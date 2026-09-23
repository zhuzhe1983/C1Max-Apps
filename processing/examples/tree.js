// Recursive Tree, Daniel Shiffman / Processing examples (public domain).
// C1Max adaptation: fixed 400x145 canvas and touch-driven branch angle.
let theta;
function setup() { size(400,145); }
function draw() {
  background(16,25,38);
  theta=radians(map(mouseX,0,width,8,80));
  stroke(147,220,195);
  translate(width/2,height);
  line(0,0,0,-47);
  translate(0,-47);
  branch(47);
}
function branch(h) {
  h*=0.66;
  if(h>1.5) {
    pushMatrix();rotate(theta);line(0,0,0,-h);translate(0,-h);branch(h);popMatrix();
    pushMatrix();rotate(-theta);line(0,0,0,-h);translate(0,-h);branch(h);popMatrix();
  }
}
