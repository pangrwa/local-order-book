#include <iostream>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include "json.hpp"

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>
    context_ptr;

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using json = nlohmann::json;

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
//   std::string payload = "";
//   c->send(con, payload, websocketpp::frame::opcode::text);
}

void on_message(websocketpp::connection_hdl, client::message_ptr msg) {
    std::string payload = msg->get_payload();
    
    if (payload.empty()) {
        std::cerr << "Received empty message." << std::endl;
        return;
    }

    try {
        // auto json_message = json::parse(payload);
        std::cout << "Parsed message: " << json::parse(payload) << std::endl;
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

int main(int argc, char* argv[]) {
  client c;



  std::string hostname = "stream.binance.com:9443/ws/bnbbtc@depth";
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
    c.set_error_channels(websocketpp::log::elevel::all);  // Enable detailed error logging
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
