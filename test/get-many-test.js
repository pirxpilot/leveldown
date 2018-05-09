const test       = require('tape')
    , testCommon = require('abstract-leveldown/testCommon')
    , leveldown  = require('..')
    , async      = require('async')


let db;

test('setUp common', testCommon.setUp)

test('setUp db', function (t) {
  db = leveldown(testCommon.location())
  db.open(t.end.bind(t))
})


test('get many', function (t) {
  let keys =  [ 1, 3, 5, 8 ].map(x => Buffer.from([x]));
  let values = [ 'a', 'c', 'd', 'e' ].map(x => Buffer.from(x));

  t.plan(1);

  function init(fn) {
    async.eachOf(
      keys,
      (key, i, fn) => db.put(key, values[i], fn),
      fn
    );
  }

  function check(result, fn) {
    t.deepEqual(result, values);
    fn();
  }

  async.waterfall([
    init,
    fn => db.getMany(keys, fn),
    check
  ], err => t.end(err))
})

test('tearDown', function (t) {
  db.close(testCommon.tearDown.bind(null, t));
});

