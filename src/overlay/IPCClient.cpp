// SPDX-License-Identifier: AGPL-3.0-only
// Modified by Shinyflvres, 2026-08-23. Part of SpaceSync, a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0). See NOTICE.md

#include "IPCClient.h"

#include <string>
#include <stdexcept>

static std::string LastErrorString(DWORD lastError)
{
	LPSTR buffer = nullptr;
	size_t size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, NULL
	);

	std::string message(buffer, size);
	LocalFree(buffer);
	return message;
}

IPCClient::~IPCClient()
{
	if (pipe && pipe != INVALID_HANDLE_VALUE)
		CloseHandle(pipe);
}

void IPCClient::Connect()
{
	const int maxAttempts = 3;
	DWORD delayMs = 2000;

	for (int attempt = 1; attempt <= maxAttempts; ++attempt)
	{
		try
		{
			ConnectInternal();
			return;
		}
		catch (const std::runtime_error& e)
		{
			if (pipe && pipe != INVALID_HANDLE_VALUE)
				CloseHandle(pipe);
			pipe = INVALID_HANDLE_VALUE;

			if (attempt >= 3)
				throw;

			fprintf(stderr, "IPC connect failed (attempt %d/3), retrying in %lums: %s\n", attempt, delayMs, e.what());
			Sleep(delayMs);
			delayMs *= 2;
		}
	}
}

bool IPCClient::TryConnect()
{
	try
	{
		ConnectInternal(100);
		return true;
	}
	catch (const std::runtime_error&)
	{
		Disconnect();
		return false;
	}
}

void IPCClient::Disconnect()
{
	if (pipe && pipe != INVALID_HANDLE_VALUE)
		CloseHandle(pipe);
	pipe = INVALID_HANDLE_VALUE;
}

protocol::Response IPCClient::SendBlocking(const protocol::Request &request)
{
	if (!IsConnected())
		return protocol::Response(protocol::ResponseInvalid);
	try
	{
		Send(request);
		return Receive();
	}
	catch (const std::runtime_error&)
	{
		Disconnect();
		return protocol::Response(protocol::ResponseInvalid);
	}
}

void IPCClient::Send(const protocol::Request &request)
{
	DWORD bytesWritten;
	BOOL success = WriteFile(pipe, &request, sizeof request, &bytesWritten, 0);
	if (!success)
	{
		throw std::runtime_error("Error writing IPC request. Error: " + LastErrorString(GetLastError()));
	}
}

protocol::Response IPCClient::Receive()
{
	protocol::Response response(protocol::ResponseInvalid);
	DWORD bytesRead;

	BOOL success = ReadFile(pipe, &response, sizeof response, &bytesRead, 0);
	if (!success)
	{
		DWORD lastError = GetLastError();
		if (lastError != ERROR_MORE_DATA)
		{
			throw std::runtime_error("Error reading IPC response. Error: " + LastErrorString(lastError));
		}
	}

	if (bytesRead != sizeof response)
	{
		throw std::runtime_error("Invalid IPC response with size " + std::to_string(bytesRead));
	}

	return response;
}

void IPCClient::ConnectInternal(DWORD waitMs)
{
	LPCTSTR pipeName = TEXT(SPACESYNC_PIPE_NAME);
	WaitNamedPipe(pipeName, waitMs);
	pipe = CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
	if (pipe == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("SpaceSync driver unavailable. Make sure SteamVR is running and the SpaceSync add-on is enabled in SteamVR settings.");
	}

	DWORD mode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(pipe, &mode, 0, 0))
	{
		throw std::runtime_error("Couldn't set pipe mode. Error: " + LastErrorString(GetLastError()));
	}

	Send(protocol::Request(protocol::RequestHandshake));
	auto response = Receive();

	if (response.type != protocol::ResponseHandshake || response.protocol.version != protocol::Version)
	{
		throw std::runtime_error(
			"Incorrect driver version installed, try reinstalling SpaceSync. (Client: " +
			std::to_string(protocol::Version) + ", Driver: " +
			std::to_string(response.protocol.version) + ")"
		);
	}
}
