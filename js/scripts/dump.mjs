#!/usr/bin/node --experimental-modules

import {Client} from '../index.mjs'
import {Tree, Node, Group, Variable, Action, Stream} from '../index.mjs'

var c = new Client();

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

(async () => {
  await c.connect("localhost", 28015);
  console.log('connected');
  let t = await c.get("live");
  if (t == null) {
    console.log('could not find tree');
  } else {
    while (true) {
      console.log(t.toString());
      await sleep(1000);
    }
  }
})();
console.log('done');
