#pragma once

struct Message {
	shared_ptr<vector<uint8_t>> data;
	int size;
	int written;
};

int ExtendMessage(Message *m, uint8_t* data, int size, int* shouldWrite) {
	int sz = 0;
	for (int i = 0; i < size; i++) {
		sz ++;
		if (*(data + i) == '\n') {
			*shouldWrite = 1;
			break;
		}
	}
	m->data->insert(m->data->end(), data, data + sz);
	m->size += sz;
	
	return sz;
}

bool isDataAvailableForWriting(Message* m) {
	if (m->written < m->size) return true;
	return false;
}

using MessageQueue = queue<Message>;
struct ClientWorker {
	int socket;
	MessageQueue messages;
	Message currMessage;
	Message inMessage;

	ClientWorker(int socket)
 		:socket(socket)	{
			currMessage = Message{ size: 0, written: 0};	
			inMessage = Message{ 
				data: make_shared<vector<uint8_t>>(), 
				size: 0, 
				written: 0,
			};	
		}
};

int SendMessage(ClientWorker *c) {
	if (isDataAvailableForWriting(&c->currMessage)) {
		int prevWritten = c->currMessage.written;
		void* data = (void*) (&c->currMessage.data->data()[prevWritten]);
		int leftSize = c->currMessage.size - prevWritten;

		int written = send(c->socket, (void*) data, leftSize, MSG_NOSIGNAL);
		if (written <= 0) {
			if (written == EAGAIN || errno == EPIPE) {
				return written;
			}
			perror("Writing Error: ");
			return -1;
		}

		c->currMessage.written += written;
		return 0;
	}

	if (c->messages.size() <= 0) return 0;
	c->currMessage = c->messages.front();
	c->messages.pop();
	return SendMessage(c);
}

int AddMessage(ClientWorker *c, Message m) {
	c->messages.push(m);
	
	// we try to write in case socket is writeable
	int r = SendMessage(c);
	if (r == EAGAIN) return 0;
	return r;
}
