// Koch Curve, Daniel Shiffman / Processing examples (public domain).
// C1Max adaptation: level advances every 30 frames; tap restarts.
let segments=[],level=0;
function setup() { restart(); }
function restart() { level=0;segments=[[new PVector(10,130),new PVector(390,130)]]; }
function nextLevel() {
  let next=[];
  for(let [a,e] of segments) {
    let third=PVector.sub(e,a).div(3);
    let b=a.copy().add(third),d=e.copy().sub(third);
    let c=b.copy().add(third.copy().rotate(-PI/3));
    next.push([a,b],[b,c],[c,d],[d,e]);
  }
  segments=next;level++;
}
function draw() {
  background(17,25,40);stroke(116,199,241);noFill();
  for(let [a,b] of segments)line(a.x,a.y,b.x,b.y);
  if(frameCount%30===0) { if(level<5)nextLevel();else restart(); }
}
function mousePressed() { restart(); }
