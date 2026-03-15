document.addEventListener('DOMContentLoaded', function() {
  var form = document.querySelector('form');
  if (form) {
    form.addEventListener('submit', function() {
      var btn = form.querySelector('button');
      if (btn) {
        btn.textContent = 'Logging in...';
        btn.disabled = true;
      }
    });
  }
});
