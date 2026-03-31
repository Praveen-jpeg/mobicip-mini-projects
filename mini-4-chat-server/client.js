const statusEl = document.getElementById("status");
const usernameInput = document.getElementById("username");
const joinBtn = document.getElementById("joinBtn");
const messagesEl = document.getElementById("messages");
const chatForm = document.getElementById("chatForm");
const chatModeSelect = document.getElementById("chatModeSelect");
const recipientInput = document.getElementById("recipientInput");
const messageInput = document.getElementById("messageInput");
/* socket tracks the active WebSocket connection.
   joined becomes true after the server accepts the username. */
let socket;
let joined = false;
let currentUsername = "";
/* Update the status line under the join box. */
function setStatus(text, isError = false) {
  statusEl.textContent = text;
  statusEl.style.color = isError ? "#b42318" : "";
}
function isSocketOpen() {
  return socket && socket.readyState === WebSocket.OPEN;
}
function setChatMode(mode) {
  const isPrivate = mode === "private";
  recipientInput.disabled = !isPrivate;
  if (!isPrivate) recipientInput.value = "";
  messageInput.placeholder = isPrivate ? "Type a private message" : "Type a public message";
}
/* Render a single chat event card inside the messages area. */
function addMessage(type, sender, recipient, message) {
  const card = document.createElement("article");
  card.className = `message ${type || "system"}`;
  const title = document.createElement("div");
  title.className = "message-title";
  title.textContent = sender || "server";
  if (type === "private") {
    title.textContent = `${sender || "user"} -> ${recipient || "user"}`;
  }
  const body = document.createElement("div");
  body.className = "message-body";
  body.textContent = message || "";
  card.appendChild(title);
  card.appendChild(body);
  messagesEl.appendChild(card);
  /* Keep the newest message visible without manual scrolling. */
  messagesEl.scrollTop = messagesEl.scrollHeight;
}
/* Open the real-time connection that the browser will use for chat events. */
function connect() {
  const protocol = window.location.protocol === "https:" ? "wss" : "ws";
  /* Reuse the same host as the page, but switch from HTTP to WebSocket. */
  socket = new WebSocket(`${protocol}://${window.location.host}/ws`);
  socket.addEventListener("open", () => {
    /* The connection exists now, but the user still needs to log in. */
    setStatus("Connected. Enter a username to join.");
  });
  socket.addEventListener("close", () => {
    /* If the socket closes, the current page can no longer send messages. */
    setStatus("Disconnected from server.", true);
    addMessage("error", "server", "", "WebSocket connection closed.");
  });
  socket.addEventListener("message", (event) => {
    const data = JSON.parse(event.data);
    /* The server sends JSON events for login status, errors, and chat messages. */
    if (data.type === "login") {
      joined = true;
      currentUsername = data.sender || "";
      usernameInput.disabled = true;
      joinBtn.disabled = true;
      setStatus(`Logged in as ${data.sender}`);
    } else if (data.type === "error") {
      setStatus(data.message, true);
    }
    addMessage(data.type, data.sender, data.recipient, data.message);
  });
}
/* Join button sends the chosen username to the server. */
joinBtn.addEventListener("click", () => {
  const username = usernameInput.value.trim();
  if (!username) {
    setStatus("Please enter a username.", true);
    return;
  }
  if (!isSocketOpen()) {
    setStatus("Socket is not connected yet.", true);
    return;
  }
  /* LOGIN tells the server to reserve this username for the socket. */
  socket.send(`LOGIN|${username}`);
});
chatModeSelect.addEventListener("change", () => {
  setChatMode(chatModeSelect.value);
});
/* Form submit sends a public chat message through the same WebSocket. */
chatForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const message = messageInput.value.trim();
  if (!message) {
    return;
  }
  if (!joined) {
    setStatus("Join the chat first.", true);
    return;
  }
  if (!isSocketOpen()) {
    setStatus("Socket is not connected.", true);
    return;
  }
  if (chatModeSelect.value === "private") {
    const recipient = recipientInput.value.trim();
    if (!recipient) {
      setStatus("Enter a recipient username for private chat.", true);
      return;
    }
    if (recipient.toLowerCase() === currentUsername.toLowerCase()) {
      setStatus("Choose another user for private chat.", true);
      return;
    }
    socket.send(`PRIVATE|${recipient}|${message}`);
  } else {
    /* PUBLIC broadcasts the message to everyone currently connected. */
    socket.send(`PUBLIC|${message}`);
  }
  messageInput.value = "";
});
setChatMode("public");
connect();
