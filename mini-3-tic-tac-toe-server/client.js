"use strict";

const ws=new WebSocket("ws://localhost:9000");

const board=document.getElementById("board");
const statusText=document.getElementById("status");

let myTurn=false;
let gameOver=false;
let mySymbol="";

const cells=[];

for(let i=0;i<9;i++){

const cell=document.createElement("div");
cell.className="cell";

cell.onclick=()=>sendMove(i);

board.appendChild(cell);

cells.push(cell);
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

const msg=e.data.split(" ");

switch(msg[0]){

case "INFO":
mySymbol=msg[3];
statusText.textContent=`You are ${mySymbol}`;
break;

case "MOVE":
cells[msg[1]].textContent=msg[2];
break;

case "YOUR_TURN":
myTurn=true;
statusText.textContent="Your Turn";
break;

case "OPPONENT_TURN":
myTurn=false;
statusText.textContent="Opponent Turn";
break;

case "WIN":
gameOver=true;
statusText.textContent="🎉 You Win!";
break;

case "LOSE":
gameOver=true;
statusText.textContent="😢 You Lose!";
break;

case "DRAW":
gameOver=true;
statusText.textContent="🤝 Draw!";
break;

case "RESTART":
resetBoard();
statusText.textContent="New Game Started";
break;
}
};