module.exports = {
  exec: function(process, console) {
    global.console.log('argv:', process.argv);
    console.log('argv:', process.argv);
    process.exit(0);
  }
}

