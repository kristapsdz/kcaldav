(function(root) {
	'use strict';

	var res = null;

	function loadSuccess(resp)
	{

		if (null === (res = parseJSON(resp)))
			return;

		classSet(document.getElementById('loading'), 'hide');
		classUnset(document.getElementById('loaded'), 'hide');

		principalFill(res.principal);

		if ('https:' === location.protocol)
			classSet(document.getElementById('insecure'), 'hide');
		else
			classUnset(document.getElementById('insecure'), 'hide');
	}

	function loadSetup()
	{

		classUnset(document.getElementById('loading'), 'hide');
		classSet(document.getElementById('loaded'), 'hide');
	}

	function load()
	{

		sendQuery('@CGIURI@/index.json', 
			loadSetup, loadSuccess, null);
	}

	function setemailError(code)
	{
		var cls;

		cls = 'setemail-error-sys';
		if (400 === code)
			cls = 'setemail-error-form';

		classUnset(document.getElementById(cls), 'hide');
		classUnset(document.getElementById('setemail-btn'), 'hide');
		classSet(document.getElementById('setemail-pbtn'), 'hide');
	}

	function setemail(e)
	{

		return(sendForm(e, function() { genericSetup('setemail'); }, 
			setemailError, function() { document.location.reload(); }));
	}

	function setpassError(code)
	{
		var cls;

		cls = 'setpass-error-sys';
		if (400 === code)
			cls = 'setpass-error-form';

		classUnset(document.getElementById(cls), 'hide');
		classUnset(document.getElementById('setpass-btn'), 'hide');
		classSet(document.getElementById('setpass-pbtn'), 'hide');
	}

	function setpass(e)
	{

		return(sendForm(e, function() { genericSetup('setpass'); }, 
			setpassError, function() { document.location.reload(); }));
	}

	function modproxyError(code)
	{
		var cls;

		cls = 'modproxy-error-sys';
		if (400 === code)
			cls = 'modproxy-error-form';

		classUnset(document.getElementById(cls), 'hide');
		classUnset(document.getElementById('modproxy-btn'), 'hide');
		classSet(document.getElementById('modproxy-pbtn'), 'hide');
	}

	function modproxy(e)
	{

		return(sendForm(e, function() { genericSetup('modproxy'); }, 
			modproxyError, function() { document.getElementById('modproxy').reset(); document.location.reload(); }));
	}

	function newcolnError(code)
	{
		var cls;

		cls = 'newcoln-error-sys';
		if (400 === code)
			cls = 'newcoln-error-form';

		classUnset(document.getElementById(cls), 'hide');
		classUnset(document.getElementById('newcoln-btn'), 'hide');
		classSet(document.getElementById('newcoln-pbtn'), 'hide');
	}

	function newcoln(e)
	{

		return(sendForm(e, function() { genericSetup('newcoln'); }, 
			newcolnError, function() { document.location.reload(); }));
	}

	function updateHash(input) 
	{
		var e = document.getElementById('password');

		if ('' == input.value)
			e.value = '';
		else 
			e.value = md5(res.principal.name + ':kcaldav:' + input.value);
	}

	function genericSetup(name)
	{
		var list, i, sz;

		list = document.getElementsByClassName(name + '-error');
		for (i = 0, sz = list.length; i < sz; i++)
			classSet(list[i], 'hide');
		classSet(document.getElementById(name + '-btn'), 'hide');
		classUnset(document.getElementById(name + '-pbtn'), 'hide');
	}

	root.load = load;
	root.updateHash = updateHash;
	root.newcoln = newcoln;
	root.setemail = setemail;
	root.setpass = setpass;
	root.modproxy = modproxy;
})(this);
