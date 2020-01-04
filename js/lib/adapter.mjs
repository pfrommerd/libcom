import hrt from 'high-res-timeout';
var HighResTimeout = hrt.default;
import Signal from 'signals';

class AdapterSubscriber {
  constructor(adapter, minInterval, maxInterval) {
    this.minInterval = minInterval;
    this.maxInterval = maxInterval;
    this.data = new Signal();

    this._adapter = adapter;

    this._minTimer = new HighResTimeout(minInterval);
    this._minTimer.on('complete', () => { 
      if (this._buffered != undefined) this._notify(this._buffered);
    });
    this._buffered = undefined;
  }

  _notify(val) {
    if (!this._minTimer.running) {
      this.data.dispatch(val);
      this._buffered = undefined;
      if (this.minInterval > 0) this._minTimer.reset().start();
    } else {
      this._buffered = val;
    }
  }

  async change(minInterval, maxInterval) {
    this.minInterval = minInterval;
    this.maxInterval = maxInterval;

    this._minTimer.duration = minInterval;
    await this._adapter._recalculate();
  }

  async cancel() {
    this._minTimer.stop();
    this._adapter._subs.delete(this);
    await this._adapter._recalculate();
    this._adapter = null;
  }
}

// an adapter takes in a stream of updates
// and only does min-time fitlering, not
// republishing on max time
export class Adapter {
  // these may be asynchronous functions!
  constructor(changeSubscription, stopSubscription) {
    this._changeSubscription = changeSubscription;
    this._stopSubscription = stopSubscription;
    this._subs = new Set();

    this._minInterval = null;
    this._maxInterval = null;
    this._lastVal = undefined;
  }

  async _recalculate() {
    var min = null;
    var max = null;
    for (let s of this._subs) {
      min = min == null ? s.minInterval : Math.min(s.minInterval, min);
      max = max == null ? s.maxInterval : Math.min(s.maxInterval, max);
    }
    if (min != this._minInterval || 
       max != this._maxInterval) {
      this._minInterval = min;
      this._maxInterval = max;
      if (min == null) {
        await this._stopSubscription();
      } else {
        await this._changeSubscription(min, max);
      }
      return true;
    }
    return false;
  }

  update(val) {
    this._lastVal = val;
    for (let s of this._subs) s._notify(val);
  }

  async subscribe(minInterval, maxInterval) {
    var s = new AdapterSubscriber(this, minInterval, maxInterval);
    this._subs.add(s);
    // if the underlying subscription
    // is not changing, push a value to the subscription next tick
    if (!(await this._recalculate())) {
      setTimeout(() => { 
        if (this._lastVal != undefined) s._notify(this._lastVal);
      }, 0);
    }
    return s;
  }
}

