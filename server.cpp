#include <iostream>
#include <asio.hpp>
#include <string>
#include <map>
#include <ctime>
template<typename T>
struct Hub {
	std::map<std::string, std::shared_ptr<T>> identity_map_;
	static std::map<std::string, std::shared_ptr<T>>& map() {
		static Hub hub_{};
		return hub_.identity_map_;
	}
};

class Connection:public std::enable_shared_from_this<Connection> {
public:
	std::string con_type = "";
	std::string client_sn_;
public:
	Connection(asio::io_service& io):socket_(io), buffer_(2048){

	}
	~Connection() {
		std::cout << "destory" << std::endl;
	}
	void close() {
		std::error_code ec0;
		socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec0);
		std::error_code ec;
		socket_.close(ec);
	}
public:
	std::size_t left_size() {
		return (buffer_.size() - read_pos_);
	}
	void set_buffer_pos(std::size_t size) {
		read_pos_ += size;
	}
	void read() {
		socket_.async_read_some(asio::buffer(&buffer_[0],buffer_.size()), [con = this->shared_from_this()](std::error_code const& ec,std::size_t readSize) {
			if (ec) {
				return;
			}
			con->parse();
		});
	}
	void brower_read(std::shared_ptr<Connection> corressponding) { //读取内容写入corressponding
		socket_.async_read_some(asio::buffer(&buffer_[0], buffer_.size()), [nowCon = this->shared_from_this(), corressponding](std::error_code const& ec, std::size_t readSize) {
			if (ec) {
				std::cout << "nat close" << std::endl;
				corressponding->close();
				nowCon->close();
				return;
			}
			auto& buffers = nowCon->get_buffer();
			//std::cout << buffers.data() << std::endl;
			corressponding->extern_write(nowCon,std::make_shared<std::vector<char>>(buffers.begin(), buffers.begin()+strlen(buffers.data()))); //写给corressponding
			memset(buffers.data(), 0, buffers.size());
		});
	}
	std::vector<char>& get_buffer() {
		return buffer_;
	}
	void extern_write(std::shared_ptr<Connection> corressponding,std::shared_ptr<std::vector<char>> buff) {
		asio::async_write(socket_, asio::buffer(buff->data(), buff->size()), [buff, corressponding, nowCon = this->shared_from_this()](std::error_code const& ec,std::size_t Size) {
			if (ec) {
				return;
			}
			corressponding->brower_read(nowCon);
			std::cout << "writed size: " << Size << std::endl;
			//nowCon->brower_read(corressponding);
		});
	}
	void parse() {
		auto str = std::string(&buffer_[0], sizeof("newok") - 1);
		if (str == "newok") {
			auto pos = sizeof("newok&") - 1;
			auto sn = std::string(&buffer_[pos], buffer_.size());
			sn = std::string(sn.data(), strlen(sn.c_str()));
			auto& map = Hub<Connection>::map();
			std::cout << "sn " << sn << std::endl;
			memset(buffer_.data(), 0, buffer_.size());
			auto iter = map.find(sn);
			if (iter == map.end()) {
				return;
			}
			client_sn_ = sn;
			auto brower_conn =iter->second;
			this->brower_read(brower_conn);
			brower_conn->brower_read(this->shared_from_this());
			/// 发起本地请求
		}
		else {  //本地请求的回应

		}
	}
	void create_con(int sn) {
		std::string data = "new&" + std::to_string(sn);
		auto s = std::make_shared<std::string>(data);
		asio::async_write(socket_, asio::buffer(s->data(), s->size()),[con = this->shared_from_this()](std::error_code const& ec,std::size_t) {
			if (ec) {
				return;
			}
			con->read();
		});
	}
	void set_sn(int sn) {
		sn_number = sn;
	}
	int get_sn() {
		return sn_number;
	}
public:
	asio::ip::tcp::socket& get_socket() {
		return socket_;
	}
private:
	asio::ip::tcp::socket socket_;
	std::vector<char> buffer_;
	std::size_t read_pos_ = 0;
	Connection* raw_con_;
	int sn_number;
};
enum server_type {
	nat_speclial,
	hub_nat_client,
	hub_user
};
class Server {
public:
	Server(std::size_t thread_number_, server_type is_hub_server):is_hub_server_(is_hub_server),thread_pool_(thread_number_), io_vec_(thread_number_), worker_(thread_number_), acceptor(get_io()){

	}
public:
	bool listen(std::string const& host,std::string const& port) {
		listen_host_ = host;
		listen_port_ = port;
		asio::ip::tcp::resolver::query query(host, port);
		return listen(query);
	}
private:
	bool listen(asio::ip::tcp::resolver::query query) {
		bool result = true;
		asio::ip::tcp::resolver resolver_(get_io());
		auto resolve = resolver_.resolve(query);
		for (; resolve != asio::ip::tcp::resolver::iterator(); ++resolve) {
			asio::ip::tcp::endpoint endpoint = *resolve;
			try {
				acceptor.open(endpoint.protocol());
				acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
				acceptor.bind(endpoint);
				acceptor.listen();
				start_accept();
			}
			catch (std::exception const& ec) {
				result = false;
			}
		}
		return result;
	}
public:
	std::shared_ptr<Connection> get_nat_client() {
		return nat_client_;
	}
	void call_client_create_con() {
		
	}
	void set_nat_server(Server* server) {
		nat_server_ = server;
		
	}
public:
	void run() {
		for (auto i = 0; i < thread_pool_.size(); i++) {
			worker_[i] = std::make_unique<asio::io_service::work>(io_vec_[i]);
			thread_pool_[i] = std::make_unique<std::thread>([&io = io_vec_[i]]() {
				io.run();
			});
		}
	}
public:
	~Server() {
		for (auto& iter : thread_pool_) {
			iter->join();
		}
	}
private:
	void start_accept() {
		auto con = std::make_shared<Connection>(get_io());
		if (is_hub_server_ == server_type::hub_user) {
			con->set_sn(id++);
		}
		acceptor.async_accept(con->get_socket(), [this, con](std::error_code const& ec) {
			if (ec) {
				return;
			}
			if (is_hub_server_ == server_type::hub_user) {
				Hub<Connection>::map().insert(std::make_pair(std::to_string(con->get_sn()), con));
				con->con_type = "brower";
				if (nat_server_) {
					nat_server_->nat_client_->create_con(con->get_sn());
				}
			}
			else if(is_hub_server_ == server_type::nat_speclial){
				nat_client_ = con;
			}
			else {
				con->con_type = "nat_client";
				con->read();
			}
			start_accept();
		});
	}
private:
	asio::io_service& get_io() {
		if (io_index_ >= io_vec_.size()) {
			io_index_ = 0;
		}
		return io_vec_[io_index_++];
	}
private:
	server_type is_hub_server_;
	std::size_t io_index_{ 0 };
	std::vector<asio::io_service> io_vec_;
	std::vector<std::unique_ptr<asio::io_service::work>> worker_;
	std::string listen_host_;
	std::string listen_port_;
	std::vector<std::unique_ptr<std::thread>> thread_pool_;
	asio::ip::tcp::acceptor acceptor;
	std::shared_ptr<Connection> nat_client_;
	std::atomic<std::size_t> id{ 0 };
	Server* nat_server_;
};
int main() {
	Server nat_unique_server(1, server_type::nat_speclial);   //nat 特殊服务器
	nat_unique_server.listen("0.0.0.0", "8080");
	nat_unique_server.run();

	Server server(5, server_type::hub_nat_client);  //client server
	server.listen("0.0.0.0", "8090");
	server.run();

	Server server_user(5, server_type::hub_user);
	server_user.listen("0.0.0.0", "9090");
	server_user.set_nat_server(&nat_unique_server);
	server_user.run();
	std::getchar();
}
