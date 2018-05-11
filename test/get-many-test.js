const test       = require('tape')
    , testCommon = require('abstract-leveldown/testCommon')
    , leveldown  = require('..')
    , async      = require('async')


let db;
let keys =  [ 1, 3, 5, 8 ].map(x => Buffer.from([x]));
let values = [ 'a', 'c', 'd', 'e' ].map(x => Buffer.from(x));

test('setUp common', testCommon.setUp);

test('setUp db', function (t) {
  db = leveldown(testCommon.location())
  db.open(err => t.end(err))
});

test('init DB', function (t) {
  async.eachOf(
    keys,
    (key, i, fn) => db.put(key, values[i], fn),
    err => t.end(err)
  );
});

test('get many', function (t) {
  t.plan(1);

  function check(err, result) {
    t.deepEqual(result, values);
    t.end(err);
  }

  db.getMany(keys, check);
});


test('get many with missing indexes', function (t) {
  // 1 and 3 do not represent the keys
  let someKeys =  [ 1, 13, 5, 88 ].map(x => Buffer.from([x]));

  t.plan(3);

  function check(err, result, missing) {
    t.ok(err.message.startsWith('NotFound: '));
    t.deepEqual(missing, [ 1, 3 ]);
    t.deepEqual(result, [ values[0], Buffer.alloc(0), values[2], Buffer.alloc(0)]);
    t.end();
  }

  db.getMany(someKeys, check)
});


test('tearDown', function (t) {
  db.close(() => testCommon.tearDown(t));
});

