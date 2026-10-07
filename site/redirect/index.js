// The old address (mdmm.nativekloud.com) and www.mdmm.dev: a permanent redirect to the same path on mdmm.dev.
export default {
	fetch(request)
	{
		const url = new URL(request.url);
		return Response.redirect("https://mdmm.dev" + url.pathname + url.search, 301);
	}
};
