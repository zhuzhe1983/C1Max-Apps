// Flocking, Daniel Shiffman / Processing examples (public domain).
// Same separation/alignment/cohesion rules; 42 initial boids, at most 64.
let boids=[];
class Boid {
  constructor(x,y) {
    this.position=new PVector(x,y);let a=random(TWO_PI);
    this.velocity=new PVector(cos(a),sin(a));this.maxspeed=1.5;this.maxforce=.035;
  }
  steer(desired) { return desired.normalize().mult(this.maxspeed).sub(this.velocity).limit(this.maxforce); }
  flock() {
    let sep=new PVector(),ali=new PVector(),coh=new PVector(),close=0,near=0;
    for(let other of boids) {
      let d=PVector.dist(this.position,other.position);
      if(d>0&&d<12) { sep.add(PVector.sub(this.position,other.position).normalize().div(d));close++; }
      if(d>0&&d<35) { ali.add(other.velocity);coh.add(other.position);near++; }
    }
    let acceleration=new PVector();
    if(close&&sep.mag()>0)acceleration.add(this.steer(sep.div(close)).mult(1.5));
    if(near) { acceleration.add(this.steer(ali.div(near)));acceleration.add(this.steer(coh.div(near).sub(this.position))); }
    this.velocity.add(acceleration).limit(this.maxspeed);this.position.add(this.velocity);
    this.position.x=(this.position.x+width)%width;this.position.y=(this.position.y+height)%height;
  }
  render() {
    pushMatrix();translate(this.position.x,this.position.y);rotate(this.velocity.heading2D()+HALF_PI);
    fill(120,213,210);noStroke();triangle(0,-4,-2,3,2,3);popMatrix();
  }
}
function setup() { for(let i=0;i<42;i++)boids.push(new Boid(random(width),random(height))); }
function draw() { background(17,25,40);for(let b of boids){b.flock();b.render();} }
function mousePressed() { if(boids.length<64)boids.push(new Boid(mouseX,mouseY)); }
