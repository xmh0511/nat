#include <iostream>
#include <asio.hpp>
class client {
public:
	client() :buff_(2048) {
		//std::thread t0([this]() {
		//	while (true) {
		//		std::this_thread::sleep_for(std::chrono::seconds(1));
		//		if (!socket_->is_open()) {
		//			std::cout << "socket close" << std::endl;
		//		}
		//		else {
		//			std::cout << "socket open" << std::endl;
		//		}
		//	}
		//});
		//t0.detach();
	}
public:
	void read() {
		socket_->async_read_some(asio::buffer(buff_.data(), buff_.size()), [this](std::error_code const& ec, std::size_t size) {
			//std::cout << "get some data " << std::endl;
			if (ec) {
				close();
				corresponding_client->close();
				std::cout << "close" << std::endl;
				return;
			}
			auto buf = std::make_shared<std::vector<char>>(buff_.begin(), buff_.begin()+ size);
			memset(buff_.data(), 0, buff_.size());
			corresponding_client->write(buf);
			//std::cout << sn_id_<< " prepare to write--------------------------\r\n\r\n" << buf->data() << std::endl;
			//std::error_code ig_ec;
			//asio::write(*(corresponding_client->socket_), asio::buffer(buf->data(), buf->size()), ig_ec);
			//std::cout << sn_id_<<" write end ----------------------------" << std::endl;
		});
	}
	void write(std::shared_ptr<std::vector<char>> buf) {
		asio::async_write(*socket_, asio::buffer(buf->data(), buf->size()), [buf,this](std::error_code const& ec, std::size_t size) {
			if (ec) {
				std::cout << "write close" << std::endl;
				return;
			}
			corresponding_client->read();
		});
	}
	void set_corresponding(client* client_) {
		corresponding_client = client_;
	}
	void send_nat(std::string sn) {
		auto back = "newok&" + sn;
		auto buf = std::make_shared<std::string>(back);
		asio::async_write(*socket_, asio::buffer(buf->data(), buf->size()), [buf,this](std::error_code const& ec, std::size_t size) {
			if (ec) {
				return;
			}
			read();
			corresponding_client->read();
		});
	}
public:
	void run() {
		worker_ = std::make_unique<asio::io_service::work>(io);
		thread_ = std::make_unique<std::thread>([this]() {
			io.run();
		});
	}
	void close() {
		std::error_code ec0;
		socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec0);
		std::error_code ec1;
		socket_->close(ec1);
	}
public:
	void connect(std::string const& host, std::string const& port,bool is_nat = false,std::string const& sn="") {
		asio::ip::tcp::resolver::query query(host, port);
		asio::ip::tcp::resolver resolver(io);
		auto que = resolver.resolve(query);
		socket_ = std::make_unique<asio::ip::tcp::socket>(io);
		for (; que != asio::ip::tcp::resolver::iterator(); que++) {
			asio::ip::tcp::endpoint endp = *que;
			socket_->async_connect(endp, [this, is_nat, sn](std::error_code const& ec) {
				sn_id_ = sn;
				if (is_nat) {
					send_nat(sn);
				}
			});
		}
	}
	~client() {
		if (thread_->joinable()) {
			thread_->join();
		}
	}
private:
	asio::io_service io;
	std::unique_ptr<asio::io_service::work> worker_;
	std::unique_ptr<asio::ip::tcp::socket> socket_;
	std::vector<char> buff_;
	std::unique_ptr<std::thread> thread_;
	client* corresponding_client;
	bool to_close_ = false;
	std::string sn_id_;
};

class nat_client {
public:
	nat_client():buff_(256){

	}
public:
	void read() {
		socket_->async_read_some(asio::buffer(buff_.data(), buff_.size()), [this](std::error_code const& ec,std::size_t size) {
			if (ec) {
				return;
			}
			parse();
		});
	}
	void parse() {
		auto str = std::string(&buff_[0], strlen(buff_.data()));
		auto andpos = str.find("&");
		if (andpos != std::string::npos) {
			auto command = str.substr(0, andpos);
			if (command == "new") {
				auto sn = str.substr(andpos + 1, (str.size() - 1 - andpos));
				auto client0 = new client();
				auto client1 = new client();
				client0->set_corresponding(client1);
				client1->set_corresponding(client0);
				clients_vec_.emplace_back(std::unique_ptr<client>(client0));
				clients_vec_.emplace_back(std::unique_ptr<client>(client1));
				client0->connect("127.0.0.1", "7080",false,sn);
				client1->connect("127.0.0.1", "8090",true, sn);
				client0->run();
				client1->run();
				std::cout << "create -----" << sn << std::endl;
			}
		}
		read();
	}
public:
	void run() {
		worker_ = std::make_unique<asio::io_service::work>(io);	
		thread_ = std::make_unique<std::thread>([this]() {
			io.run();
		});
	}
public:
	void connect(std::string const& host,std::string const& port) {
		asio::ip::tcp::resolver::query query(host, port);
		asio::ip::tcp::resolver resolver(io);
		auto que = resolver.resolve(query);
		socket_ = std::make_unique<asio::ip::tcp::socket>(io);
		for (; que != asio::ip::tcp::resolver::iterator(); que++) {
			asio::ip::tcp::endpoint endp = *que;
			socket_->async_connect(endp, [this](std::error_code const& ec) {
				read();
			});
		}
	}
	~nat_client() {
		if (thread_->joinable()) {
			thread_->join();
		}
	}
private:
	asio::io_service io;
	std::unique_ptr<asio::ip::tcp::socket> socket_;
	std::unique_ptr<asio::io_service::work> worker_;
	std::vector<char> buff_;
	std::unique_ptr<std::thread> thread_;
	std::vector<std::unique_ptr<client>> clients_vec_;
};
int main() {
	nat_client client;
	client.connect("127.0.0.1", "8080");
	client.run();
	std::getchar();
}
