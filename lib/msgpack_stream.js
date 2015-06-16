var util = require('util');
var Duplex = require('readable-stream').Duplex;

var bl = require('bl');
var msgpack = require('msgpack5')();


function MsgpackStream(connection) {
  Duplex.call(this, {objectMode: true});
  var mpack = this._msgpack = {
    connection: connection,
    decoder: msgpack.decoder(),
    chunks: bl(),
  };
}
util.inherits(MsgpackStream, Duplex);

MsgpackStream.prototype._read = function() {
  var conn = this._msgpack.connection;
  var chunks = this._msgpack.chunks;
  var _this = this;
  return tryUnpack();

  function tryUnpack() {
    var chunk = conn.read();
    if (chunk) chunks.append(chunk);

    try {
      var msg = msgpack.decode(chunks);
      _this.push(msg);
    } catch (err) {
      if (err instanceof msgpack.IncompleteBufferError) {
        conn.read(0);
        conn.once('readable', tryUnpack);
      } else {
        console.error(err.stack);
        _this.emit('error', err);
      }
    }
  }
};

MsgpackStream.prototype._write = function(msg, enc, cb) {
  this._msgpack.connection.write(msgpack.encode(msg), null, cb);
};


module.exports = MsgpackStream;
