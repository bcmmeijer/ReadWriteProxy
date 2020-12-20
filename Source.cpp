#include <Windows.h>
#include <array>
#include <vector>
#include <thread>
#include <type_traits>

// example imports
#include <string>
#include <iostream>

template <typename T>
concept pointer = std::is_pointer_v<T>;

template <typename Buffer>
concept Writable = requires (Buffer buf) {
	{ buf.size() } -> std::unsigned_integral;
	{ buf.data() } -> pointer;
};

class ReadWriteProxy {
public:
	ReadWriteProxy(bool init_on_create = true) {
		if (init_on_create) initialize();
	}

	~ReadWriteProxy() { destroy();	}

	enum {
		in_read = 0,
		in_write,
		out_read,
		out_write
	};

	bool initialize() {
		if (_initialized) destroy();

		SECURITY_ATTRIBUTES sattr;
		sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
		sattr.lpSecurityDescriptor = 0;
		sattr.bInheritHandle = 1;

		if (!CreatePipe(&_pipes[in_read], &_pipes[in_write], &sattr, 0))
			return false;

		if (!CreatePipe(&_pipes[out_read], &_pipes[out_write], &sattr, 0))
			return false;

		_initialized = true;
		return true;
	}

	bool destroy() {
		if (_initialized) 
			return true;
		
		bool ret = true;

		for (auto& pipe : _pipes)
			ret = CloseHandle(pipe) && ret;

		return ret;
	}

	inline const bool is_initialized() const { 
		return _initialized; 
	}

	inline const void* get_raw_pipe(uint8_t pipe) const {
		return _pipes[pipe % 4];
	}

	inline const std::pair<void*, void*> get_io_pipes() const {
		return { _pipes[in_read], _pipes[out_write] };
	}

	std::vector<uint8_t> read(size_t max_size = 1023) {
		std::vector<uint8_t> ret;
		uint8_t buffer[1024] = { 0 };
		DWORD bytes_read = 0, bytes_available = 0;

		if (!PeekNamedPipe(_pipes[out_read], buffer, 1024, &bytes_read, 
			&bytes_available, nullptr) || !bytes_available)
			return {};

		ret.resize(min(bytes_available, max_size));

		if (!ReadFile(_pipes[out_read], ret.data(), ret.size(), 
			&bytes_read, nullptr))
			return {};
		
		return ret;
	}

	std::vector<uint8_t> readall() {
		using namespace std::chrono_literals;
		std::vector<uint8_t> ret;
		while (1) {
			std::this_thread::sleep_for(100ms);
			auto r = read();
			if (!r.size()) break;
			ret.insert(ret.end(), r.begin(), r.end());
		}
		ret.push_back('\0');
		return ret;
	}
	
	template <Writable T>
	size_t write(const T& buffer) {
		DWORD written = 0;
		if (!WriteFile(_pipes[in_write], buffer.data(), buffer.size(), &written, nullptr))
			return 0;
		return written;
	}

private:
	bool _initialized = false;
	std::array<void*, 4> _pipes = { nullptr };
};

HANDLE create_process_io_redirect(char* process, void* in, void* out) {
	STARTUPINFOA si = { 0 };
	PROCESS_INFORMATION pi = { 0 };

	si.cb			= sizeof(STARTUPINFOA);
	si.dwFlags		= STARTF_USESTDHANDLES;
	si.hStdInput	= in;
	si.hStdOutput	= out;
	si.hStdError	= out;

	if (!CreateProcessA(0, process, 0, 0, 1, CREATE_NO_WINDOW, 0, 0, &si, &pi))
		return nullptr;

	if (pi.hThread) 
		CloseHandle(pi.hThread);

	return pi.hProcess;
}

int main() {

	ReadWriteProxy p;
	auto [i, o] = p.get_io_pipes();
	HANDLE process = create_process_io_redirect((char*)"powershell.exe", i, o);
	if (!process) return -1;

	// example usage
	while (1) {
		auto data = p.readall();
		if (!data.size()) continue;
		
		printf("%s", data.data());

		std::string in;
		std::cin >> in;

		p.write(in + "\r\n");
		std::vector<uint8_t> vec;
		p.write(vec);

		if (auto e = std::string_view(in.c_str(), in.find(' ')); e == "exit" || e == "quit") 
			break;
	}
	TerminateProcess(process, 0);
	
	return 0;
}