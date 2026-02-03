//
// #1 test, link libxlwsxwriter by writing an example data file.
// #2 test lite3 communication packet.
// #3 test lite3 over serial port.
//
// scan for serial ports
// open serial port
// initiate contact
// use lite3 to communicate
// use libxlsxwriter to log data
//

#include "pch.h"

// Scan serial ports by reading the registry key:
// HKEY_LOCAL_MACHINE\HARDWARE\DEVICEMAP\SERIALCOMM
static std::vector<std::string> scanSerialPorts() {
	std::vector<std::string> ports;
	HKEY hKey = NULL;
	constexpr char kRegPath[] = "HARDWARE\\DEVICEMAP\\SERIALCOMM";

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, kRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
		// Key not found or no permission; return empty list
		return ports;
	}

	DWORD valueCount = 0;
	DWORD maxValueNameLen = 0;
	DWORD maxValueLen = 0;
	if (RegQueryInfoKeyA(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &valueCount, &maxValueNameLen, &maxValueLen, NULL, NULL) != ERROR_SUCCESS) {
		RegCloseKey(hKey);
		return ports;
	}

	// Buffers must be large enough for the largest value name/data + terminating NUL
	std::vector<char> valueName(maxValueNameLen + 1);
	std::vector<BYTE> valueData(maxValueLen + 1);

	for (DWORD i = 0; i < valueCount; ++i) {
		DWORD nameLen = (DWORD)valueName.size();
		DWORD dataLen = (DWORD)valueData.size();
		DWORD type = 0;

		LONG ret = RegEnumValueA(hKey, i,
			valueName.data(), &nameLen,
			NULL, &type,
			valueData.data(), &dataLen);
		if (ret != ERROR_SUCCESS) continue;

		if (type == REG_SZ) {
			// data is a null-terminated ANSI string like "COM3"
			std::string port(reinterpret_cast<char*>(valueData.data()), dataLen);
			// trim trailing NULs/newline
			if (!port.empty() && port.back() == '\0') {
				port.resize(std::strlen(port.c_str()));
			}
			ports.push_back(port);
		}
	}

	RegCloseKey(hKey);
	return ports;
}

// Open and configure a serial port (non-overlapped). Returns INVALID_HANDLE_VALUE on failure.
// Typical configuration: 115200 8N1; adjust as needed.
static HANDLE openSerialPort(const std::string& portName, DWORD baudRate = CBR_115200)
{
	// Windows device path for COM ports >= COM10 requires the \\.\ prefix.
	std::string devicePath = "\\\\.\\" + portName;
	HANDLE h = CreateFileA(devicePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,              // exclusive access
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (h == INVALID_HANDLE_VALUE) {
		std::cerr << "CreateFile failed for " << devicePath << " (error " << GetLastError() << ")\n";
		return INVALID_HANDLE_VALUE;
	}

	// Configure timeouts
	COMMTIMEOUTS timeouts = {};
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 10;
	if (!SetCommTimeouts(h, &timeouts)) {
		std::cerr << "SetCommTimeouts failed (error " << GetLastError() << ")\n";
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	// Configure DCB (baud, parity, data bits, stop bits)
	DCB dcb = {};
	dcb.DCBlength = sizeof(DCB);
	if (!GetCommState(h, &dcb)) {
		// Try to build a DCB string if GetCommState fails
		std::string dcbSpec = "baud=" + std::to_string(baudRate) + " parity=N data=8 stop=1";
		if (!BuildCommDCBA(dcbSpec.c_str(), &dcb)) {
			std::cerr << "BuildCommDCB failed (error " << GetLastError() << ")\n";
			CloseHandle(h);
			return INVALID_HANDLE_VALUE;
		}
	}
	else {
		dcb.BaudRate = baudRate;
		dcb.ByteSize = 8;
		dcb.Parity = NOPARITY;
		dcb.StopBits = ONESTOPBIT;
	}

	if (!SetCommState(h, &dcb)) {
		std::cerr << "SetCommState failed (error " << GetLastError() << ")\n";
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	// Clear buffers
	if (!PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT)) {
		std::cerr << "PurgeComm failed (error " << GetLastError() << ")\n";
		// Not fatal; continue
	}

	return h;
}

int main()
{
	{
		lxw_workbook* workbook = workbook_new("hello_world.xlsx");
		lxw_worksheet* worksheet = workbook_add_worksheet(workbook, NULL);

		worksheet_write_string(worksheet, 0, 0, "Hello", NULL);
		worksheet_write_number(worksheet, 1, 0, 123, NULL);

		workbook_close(workbook);
	}


	auto ports = scanSerialPorts();
	if (ports.empty()) {
		std::cout << "No serial ports found via registry.\n";
	}
	else {
		std::cout << "Found serial ports:\n";
		for (const auto& p : ports) std::cout << "  " << p << '\n';
	}

	// Example: open first found port
	if (!ports.empty()) {
		HANDLE h = openSerialPort(ports[0], CBR_115200);
		if (h != INVALID_HANDLE_VALUE) {
			std::cout << "Opened " << ports[0] << " successfully.\n";

			// Example write (send a simple string). Use WriteFile/ReadFile or overlapped IO as needed.
			const char* msg = "Hello device\r\n";
			DWORD written = 0;
			if (!WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL)) {
				std::cerr << "WriteFile failed (error " << GetLastError() << ")\n";
			}
			else {
				std::cout << "Wrote " << written << " bytes.\n";
			}

			CloseHandle(h);
		}
		else {
			std::cerr << "Failed to open " << ports[0] << '\n';
		}
	}

	return 0;
}