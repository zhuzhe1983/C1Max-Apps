// C1Max 2D Processing-style JavaScript subset. No DOM, Java, WebGL or I/O.
var width=400,height=145,frameCount=0,mouseX=200,mouseY=72,mouseIsPressed=false,key='';
var PI=Math.PI,TWO_PI=PI*2,HALF_PI=PI/2,TRIANGLES=0,CLOSE=1;
var sin=Math.sin,cos=Math.cos,sqrt=Math.sqrt,abs=Math.abs,pow=Math.pow,floor=Math.floor,ceil=Math.ceil,min=Math.min,max=Math.max,atan2=Math.atan2;
var __loop=true,__fill=0xffffffff,__stroke=0xffffffff,__matrix=[1,0,0,1,0,0],__stack=[],__shape=[];
function random(a=1,b){return b===undefined?Math.random()*a:a+Math.random()*(b-a);}
function radians(x){return x*PI/180;}
function map(x,a,b,c,d){return c+(x-a)*(d-c)/(b-a);}
function constrain(x,a,b){return max(a,min(x,b));}
function int(x){return Math.trunc(x);}
function color(r,g,b,a){if(g===undefined){g=b=r;a=255;}else if(b===undefined){a=g;g=b=r;}if(a===undefined)a=255;return ((constrain(a,0,255)<<24)|(constrain(r,0,255)<<16)|(constrain(g,0,255)<<8)|constrain(b,0,255))>>>0;}
function background(...v){__draw(0,color(...v));}
function fill(...v){__fill=color(...v);}
function stroke(...v){__stroke=color(...v);}
function noFill(){__fill=0;}
function noStroke(){__stroke=0;}
function noLoop(){__loop=false;}
function loop(){__loop=true;}
function size(w,h){if(w!==width||h!==height)throw Error('Canvas is fixed at 400 x 145');}
function createCanvas(w,h){size(w,h);}
function __point(x,y){let m=__matrix;return [m[0]*x+m[2]*y+m[4],m[1]*x+m[3]*y+m[5]];}
function translate(x,y){let p=__point(x,y);__matrix[4]=p[0];__matrix[5]=p[1];}
function rotate(t){let m=__matrix,c=cos(t),s=sin(t),a=m[0],b=m[1],d=m[2],e=m[3];m[0]=a*c+d*s;m[1]=b*c+e*s;m[2]=d*c-a*s;m[3]=e*c-b*s;}
function pushMatrix(){if(__stack.length>=64)throw Error('Transform stack limit');__stack.push(__matrix.slice());}
function popMatrix(){if(!__stack.length)throw Error('Transform stack underflow');__matrix=__stack.pop();}
function line(x,y,a,b){if(__stroke) __draw(1,...__point(x,y),...__point(a,b),__stroke);}
function triangle(x,y,a,b,p,q){let A=__point(x,y),B=__point(a,b),C=__point(p,q);if(__fill)__draw(3,...A,...B,...C,__fill);line(x,y,a,b);line(a,b,p,q);line(p,q,x,y);}
function rect(x,y,w,h){let A=__point(x,y),B=__point(x+w,y),C=__point(x+w,y+h),D=__point(x,y+h);if(__fill){__draw(3,...A,...B,...C,__fill);__draw(3,...A,...C,...D,__fill);}line(x,y,x+w,y);line(x+w,y,x+w,y+h);line(x+w,y+h,x,y+h);line(x,y+h,x,y);}
function ellipse(x,y,w,h=w){let p=__point(x,y);if(__fill)__draw(2,...p,w,h,__fill);if(__stroke)for(let i=0;i<24;i++){let a=i*TWO_PI/24,b=(i+1)*TWO_PI/24;line(x+cos(a)*w/2,y+sin(a)*h/2,x+cos(b)*w/2,y+sin(b)*h/2);}}
function beginShape(){__shape=[];}
function vertex(x,y){if(__shape.length>=3000)throw Error('Shape vertex limit');__shape.push([x,y]);}
function endShape(){for(let i=2;i<__shape.length;i++)triangle(...__shape[0],...__shape[i-1],...__shape[i]);__shape=[];}
class PVector {
  constructor(x=0,y=0){this.x=x;this.y=y;}
  copy(){return new PVector(this.x,this.y);}
  add(v){this.x+=v.x;this.y+=v.y;return this;}
  sub(v){this.x-=v.x;this.y-=v.y;return this;}
  mult(n){this.x*=n;this.y*=n;return this;}
  div(n){if(n){this.x/=n;this.y/=n;}return this;}
  mag(){return sqrt(this.x*this.x+this.y*this.y);}
  normalize(){return this.div(this.mag());}
  limit(n){if(this.mag()>n)this.normalize().mult(n);return this;}
  rotate(a){let x=this.x;this.x=x*cos(a)-this.y*sin(a);this.y=x*sin(a)+this.y*cos(a);return this;}
  heading2D(){return atan2(this.y,this.x);}
  static sub(a,b){return a.copy().sub(b);}
  static dist(a,b){return sqrt((a.x-b.x)**2+(a.y-b.y)**2);}
}
