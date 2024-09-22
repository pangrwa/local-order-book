#include <curl/curl.h>

#include <chrono>
#include <iostream>
#include <map>
#include <thread>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

#include "json.hpp"

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>
    context_ptr;

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using json = nlohmann::json;

const int ORDER_BOOK_SIZE = 5;
const std::string GREEN = "\033[32m";
const std::string RED = "\033[31m";
const std::string RESET = "\033[0m";
std::string BINANCE_SNAPSHOT_URL;

std::map<double, double, std::greater<double>> bids;
std::map<double, double> asks;
long lastUpdateId;

// print functions
void printBids() {
  std::cout << "Bids:" << std::endl;
  for (auto& p : bids) {
    std::cout << GREEN << "Price: " << p.first << " Quantity: " << p.second
              << RESET << std::endl;
  }
}

void printAsks() {
  std::cout << "Asks:" << std::endl;
  for (auto& p : asks) {
    std::cout << RED << "Price: " << p.first << " Quantity: " << p.second
              << RESET << std::endl;
  }
}

void printOrderBookTitle() {
  std::cout << "*********ORDERBOOK********************************"
            << std::endl;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                     std::string* s) {
  size_t total_size = size * nmemb;
  s->append((char*)contents, total_size);
  return total_size;
}

// fetch binance snapshot
std::string fetchSnapshot() {
  CURL* curl;
  CURLcode res;
  std::string read_buffer;

  curl = curl_easy_init();
  // setup curl
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, BINANCE_SNAPSHOT_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
  return read_buffer;
}

// update json book
bool initOrderBook() {
  // get initial snapshot from binance
  std::string snapshot = fetchSnapshot();
  json jsonSnapshot = json::parse(snapshot);
  // if there is an error from the HTTP request
  if (jsonSnapshot.find("code") != jsonSnapshot.end()) {
    std::cerr << "Check your crypto pair" << std::endl;
    return false;
  }

  // reset boook
  asks.clear();
  bids.clear();

  lastUpdateId = jsonSnapshot["lastUpdateId"];
  for (auto& bid : jsonSnapshot["bids"]) {
    double price = std::stod(bid[0].get<std::string>());
    double quantity = std::stod(bid[1].get<std::string>());
    bids[price] = quantity;
  }

  for (auto& ask : jsonSnapshot["asks"]) {
    double price = std::stod(ask[0].get<std::string>());
    double quantity = std::stod(ask[1].get<std::string>());
    asks[price] = quantity;
  }
  printOrderBookTitle();
  printBids();
  printAsks();
  return true;
}

void manageOrderBook(std::string side, json update) {
  double price = std::stod(update[0].get<std::string>());
  double quantity = std::stod(update[1].get<std::string>());

  if (side == "bid") {
    // if this price is within the bids orderbook
    if (bids.find(price) != bids.end()) {
      if (quantity ==
          0) {  // means this bid deal is gone, remove it from order book
        bids.erase(price);
        return;
      } else {
        bids[price] = quantity;  // update to the latest quantity
        return;
      }
    }

    // this price not in the order book
    if (quantity != 0) {
      bids.insert({price, quantity});
    }
    if (bids.size() > ORDER_BOOK_SIZE) {
      // remove the lowest price
      auto minIt = std::min_element(bids.begin(), bids.end());
      bids.erase(minIt);
    }

  } else if (side == "ask") {
    if (asks.find(price) != asks.end()) {
      if (quantity == 0) {
        asks.erase(price);
        return;
      } else {
        asks[price] = quantity;
        return;
      }
    }

    // this price not in the order book
    if (quantity != 0) {
      asks.insert({price, quantity});
    }
    if (asks.size() > ORDER_BOOK_SIZE) {
      // remove the highest price
      auto maxIt = std::max_element(asks.begin(), asks.end());
      asks.erase(maxIt);
    }
  }
}

void updateOrderBook(json jsonMessage) {
  long U = jsonMessage["U"];  // minId
  long u = jsonMessage["u"];  // maxId

  // process events only if U <= id + 1 <= u;
  if (U <= lastUpdateId + 1 && lastUpdateId + 1 <= u) {
    for (auto& bid : jsonMessage["b"]) {
      manageOrderBook("bid", bid);
    }

    for (auto& ask : jsonMessage["a"]) {
      manageOrderBook("ask", ask);
    }
    lastUpdateId = u;
  } else if (u <= lastUpdateId) {  // one of the old messages, just do nothing
    return;
  } else {  // lastUpdateId < U, means our orderbook is outdated, we need to
    initOrderBook();  // reset
  }
}

// connection handler
void on_open(websocketpp::connection_hdl hdl, client* c) {
  std::cout << "WebSocket connection opened!" << std::endl;

  websocketpp::lib::error_code ec;

  client::connection_ptr con = c->get_con_from_hdl(hdl, ec);
  if (ec) {
    std::cout << "Failed to get connection pointer: " << ec.message()
              << std::endl;
    return;
  }
}

void on_message(websocketpp::connection_hdl, client::message_ptr msg) {
  std::string payload = msg->get_payload();

  if (payload.empty()) {
    std::cerr << "Received empty message." << std::endl;
    return;
  }

  try {
    auto json_message = json::parse(payload);
    updateOrderBook(json_message);

    printOrderBookTitle();
    printBids();
    printAsks();
  } catch (const std::exception& e) {
    std::cerr << "JSON parse error: " << e.what() << std::endl;
  }
}

void on_fail(websocketpp::connection_hdl hdl) {
  std::cout << "WebSocket connection failed!" << std::endl;
}
void on_close(websocketpp::connection_hdl hdl) {
  std::cout << "WebSocket connection closed!" << std::endl;
}

// ssl logic
context_ptr on_tls_init(const char* hostname, websocketpp::connection_hdl) {
  context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(
      boost::asio::ssl::context::sslv23);

  try {
    ctx->set_options(boost::asio::ssl::context::default_workarounds |
                     boost::asio::ssl::context::no_sslv2 |
                     boost::asio::ssl::context::no_sslv3 |
                     boost::asio::ssl::context::single_dh_use);
    ctx->set_verify_mode(boost::asio::ssl::verify_none);
  } catch (std::exception& e) {
    std::cout << "TLS Initialization Error: " << e.what() << std::endl;
  }
  return ctx;
}

std::string getCryptoPair() {
  std::string pair;
  std::cout << "What crypto pair are you interested in?: ";
  std::getline(std::cin, pair);

  return pair;
}

int main(int argc, char* argv[]) {
  std::string pair = getCryptoPair();
  std::cout << "Getting order book for " << pair << " ... " << std::endl;
  // Sleep for 3 seconds for user to able to double check the correct symbol
  std::this_thread::sleep_for(std::chrono::seconds(3));

  std::string upper = pair;
  std::string lower = pair;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  BINANCE_SNAPSHOT_URL =
      "https://api.binance.com/api/v3/depth?symbol=" + upper + "&limit=5";

  if (!initOrderBook()) { // probably cryptopair is valid, can't get snapshot
    return -1;
  }

  client c;
  std::string hostname = "stream.binance.com:9443/ws/" + lower + "@depth";
  std::string uri = "wss://" + hostname;

  try {
    // Configure WebSocket++ client
    // log everything except payload
    c.set_access_channels(websocketpp::log::alevel::all);
    c.clear_access_channels(websocketpp::log::alevel::frame_payload);
    // error logging
    c.set_error_channels(websocketpp::log::elevel::all);

    // initialise asio
    c.init_asio();

    // Set message, TLS initialization, open, fail, and close handlers
    c.set_message_handler(&on_message);

    c.set_tls_init_handler(bind(&on_tls_init, hostname.c_str(), ::_1));
    c.set_open_handler(bind(&on_open, ::_1, &c));
    c.set_fail_handler(bind(&on_fail, ::_1));

    c.set_close_handler(bind(&on_close, ::_1));
    c.set_error_channels(
        websocketpp::log::elevel::all);  // Enable detailed error logging
    websocketpp::lib::error_code ec;
    client::connection_ptr con = c.get_connection(uri, ec);
    if (ec) {
      std::cout << "Could not create connection because: " << ec.message()
                << std::endl;
      return 0;
    }
    // Create a connection to the specified url
    c.connect(con);

    c.run();

  } catch (websocketpp::exception const& e) {
    std::cout << "WebSocket Exception: " << e.what() << std::endl;
  }
}
