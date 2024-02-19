//============================================================================
// Name        : GaduGadu
// Author      : Szymon Bandel
//============================================================================

/* ### INFO ###
 *
 * First package contains request type (1B)
 *
 * Second package contains information in format:
 * UserSrcID-UserDstID-YYYY-MM-DD-HH-MM-SS
 *    1B    1B   1B  1B       19B		== 23B
 *
 * Third (or more if required) contain message buffer
 *
 *
 * Building command:
 *   g++ -Wall main.cpp -o server
 */


#include <iostream>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <exception>
#include <sys/select.h>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iomanip>


#define MAX_CLIENT_SIZE 10
#define USERS_COUNT 	4  // Static set count of user as 4
#define END_OF_STRING 	'\0'
#define FIRST_PACKAGE_LENGTH	23  // B
#define MAX_CHARS_READ_ONCE		4096  // B
#define MAX_CHARS_WRITE_ONCE	4096  // B
#define SIZE_DIGITS_TO_SEND		5

enum REQUEST_TYPE {
	GET_HISTORY = 0,
	SEND_MESSAGE = 1
};
enum users {
	USER_NONE = -1,
	USER_1 = 0,
	USER_2 = 1,
	USER_3 = 2,
	USER_4 = 3
};
struct DateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};
struct Message {
	int userSrcId = USER_NONE;
	int userDstId = USER_NONE;
	std::string buffer{};
	DateTime date;
};

// The container that will contain the messages
std::unordered_map<std::string, std::vector<Message>> messages;


std::vector<std::string> split(const std::string& data, char delimiter) {
    std::vector<std::string> tokens{};
    std::string token;

    // Initial check
    if (data.empty()) {
    	return {};
    }

    size_t iterator = 0;
    while (iterator < data.size()) {
        while (iterator < data.size() && data[iterator] != delimiter) {
            token += data[iterator++];
        }

        tokens.push_back(token);
        token.clear();

        // Skip the delimiter if present
        if (iterator < data.size() && data[iterator] == delimiter) {
            ++iterator;
        }
    }

    return tokens;
}


std::string createMapKey(int sender, int receiver) {
	std::string key;
	try {
		std::string senderString = std::to_string(sender);
		std::string receiverString = std::to_string(receiver);
		key = senderString + '-' + receiverString;
	} catch (const std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		throw std::runtime_error("createMapKey error");
	}
	return key;
}


bool parseInfoPackage(int cfd, Message& newMessage) {
    std::string firstPackageInfo;
    firstPackageInfo.resize(FIRST_PACKAGE_LENGTH+1);

    int bytesRead = read(cfd, &firstPackageInfo[0], FIRST_PACKAGE_LENGTH);
    if (bytesRead <= 0) {
		// Handle read failure or reaching end-of-file
		return false;
	}
    firstPackageInfo[FIRST_PACKAGE_LENGTH] = END_OF_STRING;  // Add end of string

    auto dataVector = split(firstPackageInfo, '-');

    if (dataVector.size() != 8) {
		// Handle unexpected data format
		return false;
	}

	try {
		newMessage.userSrcId = std::stoi(dataVector[0]);
		newMessage.userDstId = std::stoi(dataVector[1]);
		newMessage.date.year = std::stoi(dataVector[2]);
		newMessage.date.month = std::stoi(dataVector[3]);
		newMessage.date.day = std::stoi(dataVector[4]);
		newMessage.date.hour = std::stoi(dataVector[5]);
		newMessage.date.minute = std::stoi(dataVector[6]);
		newMessage.date.second = std::stoi(dataVector[7]);
	} catch (const std::exception& e) {
		// Handle stoi conversion failure
		std::cerr << "Exception: " << e.what() << std::endl;
		return false;
	}

	// Return true if users is set
    return newMessage.userSrcId != users::USER_NONE
		&& newMessage.userDstId != users::USER_NONE;
}

void readContentUntilEnd(int cfd, Message& newMessage) {
	char tempBuff[MAX_CHARS_READ_ONCE] = {0};
	std::string messageContent;
	int alreadyReaded = 0;

	int charReaded = read(cfd, tempBuff, MAX_CHARS_READ_ONCE);
	while (charReaded > 0) {
		alreadyReaded += charReaded;
		newMessage.buffer.append(tempBuff);
		charReaded = read(cfd, tempBuff, MAX_CHARS_READ_ONCE);
	}
	newMessage.buffer.append("\0");
}

void sendPackages(int cfd, int sender, int receiver) {
	// Create keys
	std::string senderToReceiverKey = createMapKey(sender, receiver);
	std::string receiverToSenderKey = createMapKey(receiver, sender);

	// First send size of sender messages
	int size = messages[receiverToSenderKey].size();
	std::string sizeAsString = std::to_string(size);
	sizeAsString = std::string(SIZE_DIGITS_TO_SEND - sizeAsString.size(), '0') + sizeAsString;  // fill by 0 ex.: 00102
	write(cfd, sizeAsString.c_str(), SIZE_DIGITS_TO_SEND);

	// Now send date with buffer
	// YYYY_MM_DD_HH_MM_SS-BUFFER
	for (auto& message : messages[receiverToSenderKey]) {
		std::stringstream buffer;
		buffer << std::setfill('0');
		buffer << std::setw(4) << message.date.year << "_"
			   << std::setw(2) << message.date.month << "_"
			   << std::setw(2) << message.date.day << "_"
			   << std::setw(2) << message.date.hour << "_"
			   << std::setw(2) << message.date.minute << "_"
			   << std::setw(2) << message.date.second << "-"
			   << message.buffer << '\0';
		std::string dataToSend = buffer.str();

		size_t remainingToSend = dataToSend.length();
		size_t alreadySent = 0;
		while (remainingToSend > 0) {
			size_t bytesToSend = std::min(remainingToSend, (size_t)MAX_CHARS_WRITE_ONCE);
			auto written = write(cfd, dataToSend.c_str() + alreadySent, bytesToSend);
			alreadySent += written;
			remainingToSend -= written;
		}
	}

	// Second send size of receiver
	size = messages[senderToReceiverKey].size();
	sizeAsString = std::to_string(size);
	sizeAsString = std::string(SIZE_DIGITS_TO_SEND - sizeAsString.size(), '0') + sizeAsString;  // fill by 0 ex.: 00102
	write(cfd, sizeAsString.c_str(), SIZE_DIGITS_TO_SEND);


	// Now send date with buffer
	// YYYY_MM_DD_HH_MM_SS-BUFFER
	for (auto& message : messages[senderToReceiverKey]) {
		std::stringstream buffer;
		buffer << std::setfill('0');
		buffer << std::setw(4) << message.date.year << "_"
			   << std::setw(2) << message.date.month << "_"
			   << std::setw(2) << message.date.day << "_"
			   << std::setw(2) << message.date.hour << "_"
			   << std::setw(2) << message.date.minute << "_"
			   << std::setw(2) << message.date.second << "-"
			   << message.buffer << '\0';
		std::string dataToSend = buffer.str();

		size_t remainingToSend = dataToSend.length();
		size_t alreadySent = 0;
		while (remainingToSend > 0) {
			size_t bytesToSend = std::min(remainingToSend, (size_t)MAX_CHARS_WRITE_ONCE);
			auto written = write(cfd, dataToSend.c_str() + alreadySent, bytesToSend);
			alreadySent += written;
			remainingToSend -= written;
		}
	}
}

REQUEST_TYPE recognizeRequestType(int cfd) {
	char reqType[1] = {0};
	read(cfd, reqType, 1);
	return static_cast<REQUEST_TYPE>(reqType[0]);
}


void processClientRequest(int cfd) {
	REQUEST_TYPE requestType = recognizeRequestType(cfd);

	switch (requestType) {
		case REQUEST_TYPE::GET_HISTORY: {
			Message newMessage;
			if(!parseInfoPackage(cfd, newMessage)) {
				throw std::runtime_error("parseInfoPackage error");
			}
			// Now we know who is the sender and who is the receiver

			sendPackages(cfd, newMessage.userSrcId, newMessage.userDstId);
			break;
		}
		case REQUEST_TYPE::SEND_MESSAGE: {
			Message newMessage;
			if(!parseInfoPackage(cfd, newMessage)) {
				throw std::runtime_error("parseInfoPackage error");
			}
			readContentUntilEnd(cfd, newMessage);

			// Key ["2-4"] mean USER_1 send message to USER_3
			std::string senderToReceiverKey = createMapKey(newMessage.userSrcId, newMessage.userDstId);
			messages[senderToReceiverKey].push_back(newMessage);
			break;
		}
		default: {
			close(cfd);
			throw std::runtime_error("Unknown request type");
		}
	}
	close(cfd);
}

int main() {
	socklen_t slt;
	int sfd, cfd, fdmax, fda, rc, i, on=1;
	struct sockaddr_in saddr, caddr;
	static struct timeval timeout;
	fd_set mask, rmask, wmask;

	// Set server properties
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = INADDR_ANY;
	saddr.sin_port = htons(1234);

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1) {
		std::cerr << "Cannot create socket!" << std::endl;
		exit(EXIT_FAILURE);
	}

	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr));
	listen(sfd, 10);
	FD_ZERO(&mask);
	FD_ZERO(&rmask);
	FD_ZERO(&wmask);
	fdmax = sfd;

	std::cout << "Server started!" << std::endl;
	while (true) {
		FD_SET(sfd, &rmask);
		wmask = mask;
		timeout.tv_sec = 5*60;
		timeout.tv_usec = 0;

		// Multiplexing input/output
		rc = select(fdmax+1, &rmask, &wmask, (fd_set*)0, &timeout);
		if (rc == 0) {
			printf("Timeout!\n");
			continue;
		}
		fda = rc;
		if (FD_ISSET(sfd, &rmask)) {
			fda -= 1;
			cfd = accept(sfd, (struct sockaddr *)&caddr, &slt);
			FD_SET(cfd, &mask);
			if (cfd > fdmax) {
				fdmax = cfd;
			}
		}
		for (i = sfd+1; i <= fdmax && fda > 0; i++) {
			if (FD_ISSET(i, &wmask)) {
				fda -= 1;
				processClientRequest(i);
				FD_CLR(i, &mask);
				if (i == fdmax) {
					while(fdmax > sfd && !FD_ISSET(fdmax,&mask)) {
						fdmax-=1;
					}
				}
			}
		}
	}

	close(sfd);
	return EXIT_SUCCESS;
}
