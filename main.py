#!/usr/bin/python
import sys
import tkinter as tk
import socket
from enum import Enum
from datetime import datetime

# CONSTANTS
MAX_CHARS_READ_ONCE = 4096
MAX_CHARS_WRITE_ONCE = 4096


class REQUEST_TYPE(Enum):
    GET_HISTORY = 0
    SEND_MESSAGE = 1


SERVER_IP_ADDRESS = ""
SERVER_PORT = 0
users = ["User 1", "User 2", "User 3", "User 4"]
user_map = {user: index for index, user in enumerate(users)}  # 0: User 1  ....
currentUser = ""  # sender
selectedUserId = -1  # receiver
chat_box = None
root = None  # main window
recurringUpdates = False  # boolean value to reccuring updates messages from server


def decodeUnicode(text):
    return text.decode('utf-8', errors='ignore')


def updateChatBox(sortedTupleMess):
    if chat_box is not None:
        chat_box.config(state=tk.NORMAL)  # Enable editing
        chat_box.delete('1.0', tk.END)  # Clear existing content

        for sender_or_receiver, date, messageBuff in sortedTupleMess:
            if sender_or_receiver == 'sender':
                # Display sender's message on the right side
                chat_box.tag_configure('right', justify='right')
                chat_box.insert(tk.END, f"{messageBuff}\n", 'right')
            else:
                # Display receiver's message on the left side
                chat_box.tag_configure('left', justify='left')
                chat_box.insert(tk.END, f"{messageBuff}\n", 'left')

        chat_box.see(tk.END)  # Scroll to the end
        chat_box.config(state=tk.DISABLED)  # Disable editing


def createInfoPackage(currentUserId, selectedUserId):
    current_datetime = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    info_package = f"{currentUserId}-{selectedUserId}-{current_datetime}"
    return info_package


def readUntilChar(client_socket, c):
    # Init empty space to storage read data
    receivedData = b''
    while True:
        charRead = client_socket.recv(1)

        # If no data or found specific char -> break and return receivedData
        if not charRead or decodeUnicode(charRead) == c:
            break

        # Else add char to storage
        receivedData += charRead

    # Return decoded from utf-8 to unicode data
    return decodeUnicode(receivedData)


def writeUntilEnd(client_socket, message):
    # Encode data to bytes
    encodedMessage = message.encode()
    totalSent = 0

    # Send while end of message
    while totalSent < len(encodedMessage):
        bytesSent = client_socket.send(encodedMessage[totalSent:totalSent + MAX_CHARS_WRITE_ONCE])
        totalSent += bytesSent

        # If no data sent
        if bytesSent == 0:
            break


def getHistoryFromServer(local_selectedUserId):
    global selectedUserId

    # If receiver is different, this line will be change that
    selectedUserId = local_selectedUserId
    try:
        # Create client socket
        clientSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Connect with server
        clientSocket.connect((SERVER_IP_ADDRESS, SERVER_PORT))

        # Send type of request (first package)
        clientSocket.send(REQUEST_TYPE.GET_HISTORY.value.to_bytes(1, byteorder='big'))

        # Send info package (second package)
        infoPackage = createInfoPackage(user_map[currentUser], selectedUserId)
        clientSocket.send(infoPackage.encode())

        # Create container for all messages receiving
        # Structure looks like:
        # list(tuple(WHO, WHEN, MESSAGE))
        allMessages = list()

        # Update container by receiver to sender messages
        senderMessagesCount = int(clientSocket.recv(5).decode())
        for messageCount in range(senderMessagesCount):
            message = readUntilChar(clientSocket, '\0')
            dateString, messageBuff = message.split('-')
            year, month, day, hour, minute, second = map(int, dateString.split('_'))
            date = datetime(year, month, day, hour, minute, second)
            allMessages.append(('receiver', date, messageBuff))

        # Update container by sender to receiver messages
        receiverMessagesCount = int(clientSocket.recv(5).decode())
        for messageCount in range(receiverMessagesCount):
            message = readUntilChar(clientSocket, '\0')
            dateString, messageBuff = message.split('-')
            year, month, day, hour, minute, second = map(int, dateString.split('_'))
            date = datetime(year, month, day, hour, minute, second)
            allMessages.append(('sender', date, messageBuff))

        # Sort by date
        allMessages.sort(key=lambda x: x[1])
        updateChatBox(allMessages)

        # Close current client
        clientSocket.close()
    except Exception as e:
        print(f"An error occurred: {e}")


def sendMessageToSever(message):
    if selectedUserId != -1:
        # Create client socket
        clientSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Connect with server
        clientSocket.connect((SERVER_IP_ADDRESS, SERVER_PORT))

        # Send type of request (first package)
        clientSocket.send(REQUEST_TYPE.SEND_MESSAGE.value.to_bytes(1, byteorder='big'))

        # Send info package (second package)
        infoPackage = createInfoPackage(user_map[currentUser], selectedUserId)
        clientSocket.send(infoPackage.encode())

        # Send message
        writeUntilEnd(clientSocket, message)

        # Close current client
        clientSocket.close()

        # Refresh window
        getHistoryFromServer(selectedUserId)


def getHistoryRecurring():
    if selectedUserId != -1 and recurringUpdates:
        getHistoryFromServer(selectedUserId)
    root.after(1000, getHistoryRecurring)


def startChat():
    global chat_box, root, recurringUpdates

    # Function return user, used for dropdown menu
    def getUsersWithoutCurrent():
        return [user for user in user_map.keys() if user != currentUser]

    # Function to update checkbox
    def toggleRecurringUpdates():
        global recurringUpdates
        recurringUpdates = not recurringUpdates

    # Create main window
    root = tk.Tk()
    root.title("Chat")
    root.iconbitmap("logo.ico")
    root.resizable(width=False, height=False)

    # Create frames to organize UI elements
    users_frame = tk.Frame(root)
    users_frame.pack(side=tk.LEFT, padx=10, pady=10)
    chat_frame = tk.Frame(root)
    chat_frame.pack(side=tk.LEFT, padx=10, pady=10)

    # Checkbutton to toggle recurring updates
    toggle_checkbox = tk.Checkbutton(users_frame, text="Toggle Updates", command=toggleRecurringUpdates)
    toggle_checkbox.pack(pady=5)

    # Label for user selection
    users_label = tk.Label(users_frame, text="Select User to Chat With:")
    users_label.pack(pady=5)

    # Dropdown menu
    user_choice = tk.StringVar(root)
    user_choice.set("")  # Default selection
    user_dropdown = tk.OptionMenu(users_frame, user_choice, *getUsersWithoutCurrent())
    user_dropdown.pack()
    user_choice.trace("w", lambda *args: getHistoryFromServer(user_map[user_choice.get()]))  # callback

    # Text box for messages
    chat_box = tk.Text(chat_frame, height=15, width=50)
    chat_box.config(state=tk.DISABLED)
    chat_box.pack()

    # Entry field for typing messages
    my_msg = tk.StringVar()
    my_msg.set("")
    entry_field = tk.Entry(chat_frame, textvariable=my_msg)
    entry_field.pack()

    # Button to send messages
    send_button = tk.Button(chat_frame, text="Send", command=lambda: sendMessageToSever(my_msg.get()))
    send_button.pack()

    # Initial call for recurring updates
    root.after(1000, getHistoryRecurring)

    root.mainloop()


def select_user():
    # Create a separate window for user selection
    user_window = tk.Tk()
    user_window.resizable(width=False, height=False)
    user_window.geometry("193x120")
    user_window.iconbitmap("logo.ico")
    user_window.title("Select User")

    # Handle user selection
    def on_user_selected():
        global currentUser
        currentUser = user_choice.get()
        user_window.destroy()
        startChat()

    # Label for user selection
    user_label = tk.Label(user_window, text="Select User:")
    user_label.pack()

    # Dropdown menu
    user_choice = tk.StringVar(user_window)
    user_choice.set(users[0])  # Default selection
    user_dropdown = tk.OptionMenu(user_window, user_choice, *user_map.keys())
    user_dropdown.pack()

    # Button to confirm user selection
    select_button = tk.Button(user_window, text="Select", command=on_user_selected)
    select_button.pack()

    user_window.mainloop()


def main():
    global SERVER_IP_ADDRESS, SERVER_PORT
    args = sys.argv
    if len(args) != 3:
        print("main.py <SERVER_IP_ADDRESS> <SERVER_PORT>")
        return
    try:
        SERVER_IP_ADDRESS = args[1]
        SERVER_PORT = int(args[2])
    except Exception as e:
        print(f"An error occurred: {e}")
        return
    select_user()


if __name__ == '__main__':
    main()
