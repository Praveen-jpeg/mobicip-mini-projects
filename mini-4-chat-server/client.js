const statusEl = document.getElementById("status");
const usernameInput = document.getElementById("username");
const joinBtn = document.getElementById("joinBtn");
const messagesEl = document.getElementById("messages");
const chatForm = document.getElementById("chatForm");
const messageInput = document.getElementById("messageInput");

/* socket tracks the active WebSocket connection.
   joined becomes true after the server accepts the username. */
let socket;
let joined = false;

/* Update the status line under the join box. */
function setStatus(text, isError = false) {
  statusEl.textContent = text;
  statusEl.style.color = isError ? "#b42318" : "";
}

/* Render a single chat event card inside the messages area. */
function addMessage(type, sender, message) {
  const card = document.createElement("article");
  card.className = `message ${type || "system"}`;

  const title = document.createElement("div");
  title.className = "message-title";
  title.textContent = sender || "server";

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
    addMessage("error", "server", "WebSocket connection closed.");
  });

  socket.addEventListener("message", (event) => {
    const data = JSON.parse(event.data);

    /* The server sends JSON events for login status, errors, and chat messages. */
    if (data.type === "login") {
      joined = true;
      usernameInput.disabled = true;
      joinBtn.disabled = true;
      setStatus(`Logged in as ${data.sender}`);
    } else if (data.type === "error") {
      setStatus(data.message, true);
    }

    addMessage(data.type, data.sender, data.message);
  });
}

/* Join button sends the chosen username to the server. */
joinBtn.addEventListener("click", () => {
  const username = usernameInput.value.trim();

  if (!username) {
    setStatus("Please enter a username.", true);
    return;
  }

  if (!socket || socket.readyState !== WebSocket.OPEN) {
    setStatus("Socket is not connected yet.", true);
    return;
  }

  /* LOGIN tells the server to reserve this username for the socket. */
  socket.send(`LOGIN|${username}`);
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

  if (!socket || socket.readyState !== WebSocket.OPEN) {
    setStatus("Socket is not connected.", true);
    return;
  }

  /* PUBLIC broadcasts the message to everyone currently connected. */
  socket.send(`PUBLIC|${message}`);
  messageInput.value = "";
});

connect();
