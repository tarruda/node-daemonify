module.exports = function() {
  return {
    exec: function(process, console) {
      console.log('from .daemonify subdirectory');
      process.exit(0);
    }
  };
};
