"use strict";

const host=window.location.hostname || "127.0.0.1";
const ws=new WebSocket(`ws://${host}:9001`);

const board=document.getElementById("board");
const status=document.getElementById("status");
const newGameBtn=document.getElementById("newGameBtn");

let mySymbol="";
let myTurn=false;
let gameOver=false;

const cells=[];

for(let i=0;i<9;i++){

let cell=document.createElement("div");
cell.className="cell";

cell.onclick=()=>sendMove(i);

board.appendChild(cell);

cells.push(cell);
}

function updateStatus(turn){

status.textContent=`You are ${mySymbol} | ${turn}`;
}

function sendMove(pos){

if(!myTurn||gameOver) return;

if(cells[pos].textContent!=="") return;

ws.send(`MOVE ${pos}`);
}

function resetBoard(){

cells.forEach(c=>c.textContent="");

gameOver=false;
}

ws.onmessage=(e)=>{

let msg=e.data.split(" ");

switch(msg[0]){

case "INFO":
mySymbol=msg[1];
updateStatus("Waiting...");
break;

case "MOVE":
cells[msg[1]].textContent=msg[2];
break;

case "YOUR_TURN":
myTurn=true;
updateStatus("Your Turn");
break;

case "OPPONENT_TURN":
myTurn=false;
updateStatus("Opponent Turn");
break;

case "WIN":
gameOver=true;
status.textContent="You Win!";
break;

case "LOSE":
gameOver=true;
status.textContent="You Lose!";
break;

case "DRAW":
gameOver=true;
status.textContent="Draw!";
break;

case "OPPONENT_LEFT":
gameOver=true;
myTurn=false;
status.textContent="Opponent left. Waiting for a new game.";
break;

case "RESET":
resetBoard();
updateStatus("New Game Started");
break;
}
};

newGameBtn.onclick=()=>{
ws.send("NEW_GAME");
};
