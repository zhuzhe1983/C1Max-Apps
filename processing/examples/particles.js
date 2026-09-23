// Multiple Particle Systems, Daniel Shiffman / Processing examples (public domain).
// C1Max adaptation: bounded 3 emitters and 72 particles, tap moves emitter.
let particles=[],sources=[],emit=0;
class Particle {
  constructor(origin) {
    this.position=origin.copy();this.velocity=new PVector(random(-1,1),random(-2,0));
    this.acceleration=new PVector(0,.055);this.life=160;this.angle=0;this.crazy=random(1)>.5;
  }
  run() {
    this.velocity.add(this.acceleration);this.position.add(this.velocity);this.life-=3;
    noStroke();fill(125,205,245,this.life);ellipse(this.position.x,this.position.y,5,5);
    if(this.crazy) {
      this.angle+=this.velocity.x*.08;
      stroke(255,198,135,this.life);pushMatrix();translate(this.position.x,this.position.y);rotate(this.angle);line(0,0,9,0);popMatrix();
    }
  }
}
function setup() { sources=[new PVector(95,45),new PVector(200,30),new PVector(305,45)]; }
function draw() {
  background(17,25,40);
  if(particles.length<72)particles.push(new Particle(sources[frameCount%3]));
  for(let i=particles.length-1;i>=0;i--){particles[i].run();if(particles[i].life<0||particles[i].position.y>height+10)particles.splice(i,1);}
}
function mousePressed() { sources[emit++%3]=new PVector(mouseX,mouseY); }
