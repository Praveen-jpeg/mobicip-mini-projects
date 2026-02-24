/* =====================================
   Tic Tac Toe Client (FINAL VERSION)
   ===================================== */

"use strict";

/* ---------- CONFIG ---------- */

const WS_URL = "ws://localhost:9000";

/* ---------- DOM ELEMENTS ---------- */

const boardEl = document.getElementById("board");
const statusText = document.getElementById("status");

/* ---------- GAME STATE ---------- */

let socket = null;
let mySymbol = "";
let myTurn = false;
let gameOver = false;

const cells = [];

/* ---------- CREATE BOARD ---------- */

const createBoard = () => {
    for (let i = 0; i < 9; i++) {

        const cell = document.createElement("div");
        cell.classList.add("cell");
        cell.dataset.index = i;

        cell.addEventListener("click", () => sendMove(i));

        boardEl.appendChild(cell);
        cells.push(cell);
    }
};

/* ---------- CONNECT WEBSOCKET ---------- */

const connect = () => {

    socket = new WebSocket(WS_URL);

    socket.addEventListener("open", () => {
        statusText.textContent =
            "Connected. Waiting for opponent...";
    });

    socket.addEventListener("message", handleMessage);

    socket.addEventListener("close", () => {
        statusText.textContent =
            "Disconnected from server.";
        myTurn = false;
    });

    socket.addEventListener("error", () => {
        statusText.textContent = "Connection error.";
    });
};

/* ---------- SEND MOVE ---------- */

const sendMove = (position) => {

    /* allow move ONLY if valid turn */
    if (!socket ||
        socket.readyState !== WebSocket.OPEN ||
        !myTurn ||
        gameOver)
        return;

    if (cells[position].textContent !== "")
        return; // already filled

    socket.send(`MOVE ${position}`);
};

/* ---------- HANDLE SERVER MESSAGES ---------- */

const handleMessage = (event) => {

    const message = event.data.trim();
    const parts = message.split(" ");
    const command = parts[0];

    switch (command) {

        /* PLAYER ASSIGNMENT */
        case "START":
            mySymbol = parts[1];
            statusText.textContent = `You are ${mySymbol}`;
            break;

        /* BOARD UPDATE */
        case "MOVE": {
            const index = Number(parts[1]);
            const symbol = parts[2];

            if (cells[index])
                cells[index].textContent = symbol;

            break;
        }

        /* TURN CONTROL */
        case "YOUR_TURN":
            myTurn = true;
            statusText.textContent = "✅ Your Turn";
            break;

        case "OPPONENT_TURN":
            myTurn = false;
            statusText.textContent = "⏳ Opponent's Turn";
            break;

        /* GAME RESULTS */
        case "WIN":
            gameOver = true;
            statusText.textContent = "🎉 You Win!";
            myTurn = false;
            break;

        case "LOSE":
            gameOver = true;
            statusText.textContent = "😢 You Lose!";
            myTurn = false;
            break;

        case "DRAW":
            gameOver = true;
            statusText.textContent = "🤝 Draw!";
            myTurn = false;
            break;

        default:
            console.warn("Unknown message:", message);
    }
};

/* ---------- START APPLICATION ---------- */

createBoard();
connect();