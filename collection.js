(function(root) {
	'use strict';

	function loadSuccess(resp)
	{
		var res, id, i, sz;

		if (null === (res = parseJSON(resp)))
			return;

		id = parseInt(getQueryVariable('id'));
		for (i = 0, sz = res.principal.colns.length; i < sz; i++)
			if (res.principal.colns[i].id === id)
				break;

		if (i === sz) {
			location.href = '@HTDOCS@/home.html';
			return;
		}

		classSet(document.getElementById('loading'), 'hide');
		classUnset(document.getElementById('loaded'), 'hide');
		principalFill(res.principal);
		colnFill(document, res.principal, res.principal.colns[i]);
	}

	function loadSetup()
	{

		classUnset(document.getElementById('loading'), 'hide');
		classSet(document.getElementById('loaded'), 'hide');
	}

	function load()
	{
		var id;

		if (null !== (id = getQueryVariable('id')) && 
		    NaN !== parseInt(id))
			sendQuery('@CGIURI@/index.json', 
				loadSetup, loadSuccess, null);
		else
			location.href = '@HTDOCS@/home.html';
	}

	function setcolnpropsError(code, prefix)
	{
		var cls;

		cls = 'setcolnprops-error-sys';
		if (400 === code)
			cls = 'setcolnprops-error-form';

		classUnset(document.getElementById(cls), 'hide');
		classUnset(document.getElementById(prefix + '-btn'), 'hide');
		classSet(document.getElementById(prefix + '-pbtn'), 'hide');
	}

	function setcolnpropsSetup(name)
	{
		var list, i, sz;

		list = document.getElementsByClassName('setcolnprops-error');
		for (i = 0, sz = list.length; i < sz; i++)
			classSet(list[i], 'hide');
		classSet(document.getElementById(name + '-btn'), 'hide');
		classUnset(document.getElementById(name + '-pbtn'), 'hide');
	}

	function setcolnprops(e, prefix)
	{

		return(sendForm(e, function() { setcolnpropsSetup(prefix); }, 
			function(code) { setcolnpropsError(code, prefix); }, 
			function() { document.location.reload(); }));
	}

	root.load = load;
	root.setcolnprops = setcolnprops;
})(this);
