class OptoDSP {
  constructor(sr){
    this.sr = sr;
    this.p = {gainDb:0, peakRed:40, limit:0, emphasis:0, tube:0.3, bypass:1};
    this.fast=0; this.dens=0; this.slow=0;
    this.sx1=0; this.sx2=0; this.sy1=0; this.sy2=0;
    this.hpL={x1:0,y1:0}; this.hpR={x1:0,y1:0}; this.lpL=0; this.lpR=0;
    this.gLin=1; this.gTarget=1; this.wet=0; this.wetTarget=0;
    this.meterGR=0; this.meterPk=0; this.meterIn=0;
    this.aAtt=this.coef(0.010); this.aRelF=this.coef(0.060);
    this.dAtt=this.coef(0.5); this.dRel=this.coef(2.0);
    this.hpA = (1/(2*Math.PI*25)) / ((1/(2*Math.PI*25)) + 1/sr);
    this.lpB = 1 - Math.exp(-2*Math.PI*18000/sr);
    this.smooth = Math.exp(-1/(0.02*sr));
    this.setParams({});
  }
  coef(t){ return Math.exp(-1/(t*this.sr)); }
  setParams(np){
    Object.assign(this.p, np);
    const p=this.p;
    this.thr = 10 - 0.55*p.peakRed;
    this.ratio = p.limit ? 12 : 3.5;
    this.knee = 12;
    this.gTarget = Math.pow(10, p.gainDb/20);
    this.d = 1 + 4*p.tube;
    this.wetTarget = p.bypass ? 0 : 1;
    // high shelf RBJ, S=1
    const G = 9*p.emphasis, A = Math.pow(10, G/40), w0 = 2*Math.PI*3000/this.sr;
    const c=Math.cos(w0), s=Math.sin(w0), al = s/2*Math.SQRT2, sa = 2*Math.sqrt(A)*al;
    const a0 = (A+1)-(A-1)*c+sa;
    this.b0 = A*((A+1)+(A-1)*c+sa)/a0;
    this.b1 = -2*A*((A-1)+(A+1)*c)/a0;
    this.b2 = A*((A+1)+(A-1)*c-sa)/a0;
    this.a1 = 2*((A-1)-(A+1)*c)/a0;
    this.a2 = ((A+1)-(A-1)*c-sa)/a0;
  }
  resetMeters(){ this.meterGR=0; this.meterPk=0; this.meterIn=0; }
  process(L,R,oL,oR,n){
    const p=this.p, thr=this.thr, knee=this.knee, half=knee/2, slope=(1/this.ratio-1);
    // relâchement lent dépendant de la densité (taux de bloc)
    const relS = 0.5 + 4.5*Math.min(1, this.dens*4);
    const aRelS = this.coef(relS);
    let grMin=0, pk=this.meterPk, pin=this.meterIn;
    for(let i=0;i<n;i++){
      const l=L[i], r=R[i];
      const mi=Math.max(Math.abs(l),Math.abs(r)); if(mi>pin) pin=mi;
      // ----- sidechain -----
      const x=(l+r)*0.5;
      const y = this.b0*x + this.b1*this.sx1 + this.b2*this.sx2 - this.a1*this.sy1 - this.a2*this.sy2;
      this.sx2=this.sx1; this.sx1=x; this.sy2=this.sy1; this.sy1=y;
      const a = Math.abs(y);
      this.fast = a + (this.fast - a)*(a>this.fast ? this.aAtt : this.aRelF);
      this.dens = a + (this.dens - a)*(a>this.dens ? this.dAtt : this.dRel);
      this.slow = a + (this.slow - a)*(a>this.slow ? this.aAtt : aRelS);
      const cell = Math.max(this.fast, 0.8*this.slow);
      const ldb = 20*Math.log10(Math.max(cell,1e-6));
      const xg = ldb - thr;
      let over;
      if(xg < -half) over = 0;
      else if(xg > half) over = xg;
      else { const t=xg+half; over = t*t/(2*knee); }
      const gr = slope*over;               // dB, <= 0
      if(gr<grMin) grMin=gr;
      const g = Math.pow(10, gr/20);
      // ----- étage de sortie -----
      this.gLin += (this.gTarget - this.gLin)*(1-this.smooth);
      this.wet  += (this.wetTarget - this.wet)*(1-this.smooth);
      const d=this.d;
      let vl = l*g*this.gLin, vr = r*g*this.gLin;
      vl = Math.tanh(vl*d)/d; vr = Math.tanh(vr*d)/d;
      // HP 25 Hz
      let hl = this.hpA*(this.hpL.y1 + vl - this.hpL.x1); this.hpL.x1=vl; this.hpL.y1=hl;
      let hr = this.hpA*(this.hpR.y1 + vr - this.hpR.x1); this.hpR.x1=vr; this.hpR.y1=hr;
      // LP 18 kHz
      this.lpL += this.lpB*(hl - this.lpL); this.lpR += this.lpB*(hr - this.lpR);
      const w=this.wet;
      const outL = this.lpL*w + l*(1-w), outR = this.lpR*w + r*(1-w);
      oL[i]=outL; oR[i]=outR;
      const m = Math.max(Math.abs(outL), Math.abs(outR)); if(m>pk) pk=m;
    }
    if(grMin<this.meterGR) this.meterGR=grMin;
    this.meterPk=pk; this.meterIn=pin;
  }
}
class OptoProc extends AudioWorkletProcessor{
  constructor(){ super(); this.dsp=new OptoDSP(sampleRate); this.dsp.setParams({bypass:0}); this.cnt=0;
    this.port.onmessage=e=>this.dsp.setParams(e.data); }
  process(inputs,outputs){
    const inp=inputs[0], out=outputs[0];
    if(!inp || !inp[0] || !out || !out[0]) return true;
    const L=inp[0], R=inp[1]||inp[0], oL=out[0], oR=out[1]||out[0];
    this.dsp.process(L,R,oL,oR,L.length);
    if(++this.cnt>=6){ this.port.postMessage({gr:this.dsp.meterGR, pk:this.dsp.meterPk}); this.dsp.resetMeters(); this.cnt=0; }
    return true;
  }
}
registerProcessor('al-opto',OptoProc);
