// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <Windows.h>

#include "Protocol.h"

class IPCClient
{
public:
	~IPCClient();

	void Connect();
	bool TryConnect();
	bool IsConnected() const { return pipe && pipe != INVALID_HANDLE_VALUE; }
	protocol::Response SendBlocking(const protocol::Request &request);

	void Send(const protocol::Request &request);
	protocol::Response Receive();

private:
	void ConnectInternal(DWORD waitMs = 1000);
	void Disconnect();

	HANDLE pipe = INVALID_HANDLE_VALUE;
};