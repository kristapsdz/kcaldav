(function(root) {
	'use strict';

	function parseJSON(resp)
	{
		var res;

		try  { 
			res = JSON.parse(resp);
		} catch (error) {
			console.log('JSON parse fail: ' + resp);
			return(null);
		}

		return(res);
	}

	function sendQuery(url, setup, success, error) 
	{
		var xmh = new XMLHttpRequest();

		if (null !== setup)
			setup();

		xmh.onreadystatechange=function() {
			if (xmh.readyState === 4 && xmh.status === 200) {
				console.log(url + ': success!');
				if (null !== success)
					success(xmh.responseText);
			} else if (xmh.readyState === 4) {
				console.log(url + ': failure: ' + xmh.status);
				if (null !== error)
					error(xmh.status);
			}
		};

		console.log(url + ': sending request');
		xmh.open('GET', url, true);
		xmh.send(null);
	}

	function sendForm(form, setup, error, success) 
	{
		var xmh = new XMLHttpRequest();

		if (null !== setup)
			setup();

		xmh.onreadystatechange=function() {
			if (xmh.readyState===4 && xmh.status===200) {
				console.log(form.action + ': success!');
				if (null !== success)
					success(xmh.responseText);
			} else if (xmh.readyState === 4) {
				console.log(form.action + ': failure: ' + xmh.status);
				if (null !== error)
					error(xmh.status);
			}
		};

		console.log(form.action + ': sending form');
		xmh.open(form.method, form.action, true);
		xmh.send(new FormData(form));
		return(false);
	}

	function replacen(e, text)
	{

		if (null === e) {
			console.log('replacen: null node');
			return(null);
		}

		while (e.firstChild)
			e.removeChild(e.firstChild);
		e.appendChild(document.createTextNode(text));

		return(e);
	}

	function formatBytes(b)
	{

		if (b > 1073741824.0)
			return((b / 1073741824.0).toFixed(2) + ' GB');
		else if (b > 1048576)
			return((b / 1048576.0).toFixed(2) + ' MB');
		else
			return((b / 1024.0).toFixed(2) + ' KB');
	}

	function proxyFill(e, p, pr)
	{
		var list, i, sz;

		list = e.getElementsByClassName
			('kcalendar-proxy-email');
		for (i = 0, sz = list.length; i < sz; i++) {
			replacen(list[i], pr.email);
			list[i].href = 'mailto:' + pr.email;
		}

		list = e.getElementsByClassName
			('kcalendar-proxy-read');
		for (i = 0, sz = list.length; i < sz; i++)
			if ( ! (1 === pr.bits))
				classSet(list[i], 'hide');
			else
				classUnset(list[i], 'hide');

		list = e.getElementsByClassName
			('kcalendar-proxy-write');
		for (i = 0, sz = list.length; i < sz; i++)
			if ( ! (2 === pr.bits))
				classSet(list[i], 'hide');
			else
				classUnset(list[i], 'hide');
	}

	function rproxyFill(e, p, rp)
	{
		var list, i, sz;

		list = e.getElementsByClassName
			('kcalendar-rproxy-uri');
		for (i = 0, sz = list.length; i < sz; i++)
			replacen(list[i], location.protocol + '//' + 
				window.location.hostname +
				'@CGIURI@/' + rp.name + '/');

		list = e.getElementsByClassName
			('kcalendar-rproxy-read');
		for (i = 0, sz = list.length; i < sz; i++)
			if (1 === rp.bits)
				classUnset(list[i], 'hide');
			else
				classSet(list[i], 'hide');

		list = e.getElementsByClassName
			('kcalendar-rproxy-write');
		for (i = 0, sz = list.length; i < sz; i++)
			if (2 === rp.bits)
				classUnset(list[i], 'hide');
			else
				classSet(list[i], 'hide');
	}

	function colnFill(e, p, c)
	{
		var list, i, sz;

		list = e.getElementsByClassName
			('kcalendar-coln-uri');
		for (i = 0, sz = list.length; i < sz; i++) {
			replacen(list[i], location.protocol + '//' + 
				window.location.hostname +
				'@CGIURI@/' + p.name + '/' + c.url);
			list[i].href = '@HTDOCS@/collection.html?id=' + c.id;
		}

		list = e.getElementsByClassName
			('kcalendar-coln-description');
		for (i = 0, sz = list.length; i < sz; i++)
			if ('TEXTAREA' === list[i].tagName)
				list[i].value = c.description;
			else
				replacen(list[i], c.description);

		list = e.getElementsByClassName
			('kcalendar-coln-id');
		for (i = 0, sz = list.length; i < sz; i++)
			list[i].value = c.id;

		list = e.getElementsByClassName
			('kcalendar-coln-displayname');
		for (i = 0, sz = list.length; i < sz; i++)
			if ('INPUT' === list[i].tagName)
				list[i].value = c.displayname;
			else
				replacen(list[i], c.displayname);

		list = e.getElementsByClassName
			('kcalendar-coln-colour');
		for (i = 0, sz = list.length; i < sz; i++)
			if ('INPUT' === list[i].tagName)
				list[i].value = '#' + c.colour.substring(1, 7);
			else
				replacen(list[i], c.colour);
	}

	function principalFill(p)
	{
		var list, i, j, sz, ssz, tmpl, cln;

		list = document.getElementsByClassName
			('kcalendar-principal-name');
		for (i = 0, sz = list.length; i < sz; i++) 
			replacen(list[i], p.name);

		list = document.getElementsByClassName
			('kcalendar-principal-email');
		for (i = 0, sz = list.length; i < sz; i++)
			list[i].value = p.email;

		list = document.getElementsByClassName
			('kcalendar-principal-uri');
		for (i = 0, sz = list.length; i < sz; i++) 
			replacen(list[i], 
				location.protocol + '//' + 
				window.location.hostname + 
				'@CGIURI@/' + p.name + '/');

		list = document.getElementsByClassName
			('kcalendar-principal-quota-used');
		for (i = 0, sz = list.length; i < sz; i++) 
			replacen(list[i], p.quota_used);

		list = document.getElementsByClassName
			('kcalendar-principal-quota-avail');
		for (i = 0, sz = list.length; i < sz; i++) 
			replacen(list[i], formatBytes(p.quota_avail));

		list = document.getElementsByClassName
			('kcalendar-principal-quota-used');
		for (i = 0, sz = list.length; i < sz; i++) 
			replacen(list[i], formatBytes(p.quota_used));

		list = document.getElementsByClassName
			('kcalendar-principal-rproxies');
		for (i = 0, sz = list.length; i < sz; i++) {
			tmpl = list[i].children[0];
			if (null === tmpl)
				continue;
			list[i].removeChild(tmpl);
			for (j = 0, ssz = p.rproxies.length; j < ssz; j++) {
				cln = tmpl.cloneNode(true);
				rproxyFill(cln, p, p.rproxies[j]);
				list[i].appendChild(cln);
			}
		}
		list = document.getElementsByClassName
			('kcalendar-principal-norproxies');
		for (i = 0, sz = list.length; i < sz; i++)
			if (0 === p.rproxies.length && list[i].classList.contains('hide')) 
				list[i].classList.remove('hide');
			else if (p.rproxies.length && ! list[i].classList.contains('hide'))
				list[i].classList.add('hide');

		list = document.getElementsByClassName
			('kcalendar-principal-proxies');
		for (i = 0, sz = list.length; i < sz; i++) {
			tmpl = list[i].children[0];
			if (null === tmpl)
				continue;
			list[i].removeChild(tmpl);
			for (j = 0, ssz = p.proxies.length; j < ssz; j++) {
				cln = tmpl.cloneNode(true);
				proxyFill(cln, p, p.proxies[j]);
				list[i].appendChild(cln);
			}
		}

		list = document.getElementsByClassName
			('kcalendar-principal-noproxies');
		for (i = 0, sz = list.length; i < sz; i++)
			if (0 === p.proxies.length && list[i].classList.contains('hide')) 
				list[i].classList.remove('hide');
			else if (p.proxies.length && ! list[i].classList.contains('hide'))
				list[i].classList.add('hide');

		list = document.getElementsByClassName
			('kcalendar-principal-colns');
		for (i = 0, sz = list.length; i < sz; i++) {
			tmpl = list[i].children[0];
			if (null === tmpl)
				continue;
			list[i].removeChild(tmpl);
			for (j = 0, ssz = p.colns.length; j < ssz; j++) {
				cln = tmpl.cloneNode(true);
				colnFill(cln, p, p.colns[j]);
				list[i].appendChild(cln);
			}
		}

		list = document.getElementsByClassName
			('kcalendar-principal-nocolns');
		for (i = 0, sz = list.length; i < sz; i++)
			if (0 === p.colns.length && list[i].classList.contains('hide')) 
				list[i].classList.remove('hide');
			else if (p.colns.length && ! list[i].classList.contains('hide'))
				list[i].classList.add('hide');
	}

	function getQueryVariable(variable)
	{
		var query, vars, i, pair;

		query = window.location.search.substring(1);
		vars = query.split("&");

		for (i = 0; i < vars.length; i++) {
			pair = vars[i].split("=");
			if(pair[0] == variable)
				return(pair[1]);
		}
		return(null);
	}

	function classUnset(e, val)
	{

		if (null === e) {
			console.log('cannot set class on null element');
			return(null);
		}

		if (e.classList.contains(val))
			e.classList.remove(val);

		return(e);
	}

	function classSet(e, val)
	{

		if (null === e) {
			console.log('cannot set class on null element');
			return(null);
		}

		if ( ! e.classList.contains(val))
			e.classList.add(val);

		return(e);
	}

	function logoutSuccess()
	{

		classSet(document.getElementById('loading'), 'hide');
		classSet(document.getElementById('loaded'), 'hide');
		classUnset(document.getElementById('loggedout'), 'hide');
	}

	function logout()
	{

		sendQuery('@CGIURI@/logout.json', 
			null, logoutSuccess, null);
	}

	root.sendQuery = sendQuery;
	root.sendForm = sendForm;
	root.parseJSON = parseJSON;
	root.replacen = replacen;
	root.principalFill = principalFill;
	root.colnFill = colnFill;
	root.getQueryVariable = getQueryVariable;
	root.classSet = classSet;
	root.classUnset = classUnset;
	root.logout = logout;
})(this);
