#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <cstring>

namespace webview_gui { namespace helpers {

inline void decodeBase64(const char *base64, std::vector<unsigned char> &binary) {
	auto done = [](char c) -> bool {
		return !c || c == '=';
	};
	auto unmap = [](char c) -> unsigned char {
		if (c >= 'a') return c - char('a' - 26);
		if (c >= 'A') return c - 'A';
		if (c >= '0') return c + char(52 - '0');
		return (c == '+') ? 62 : 63;
	};
	while (1) {
		char c = *(base64++);
		if (done(c)) break;
		auto v0 = unmap(c);

		c = *(base64++);
		auto v1 = unmap(c);
		// All 6 bits of v0, top 2 bits of v1
		binary.push_back((v0<<2)|(v1>>4));

		c = *(base64++);
		if (done(c)) break;
		auto v2 = unmap(c);
		// Bottom 4 bits of v1, top 4 bits of v2
		binary.push_back(((v1&0x0F)<<4)|(v2>>2));

		c = *(base64++);
		if (done(c)) break;
		auto v3 = unmap(c);
		// Bottom 2 bits of v2, all 6 bits of v3
		binary.push_back(((v2&0x03)<<6)|v3);
	}
}
inline std::vector<unsigned char> decodeBase64(const char *base64) {
	std::vector<unsigned char> binary;
	decodeBase64(base64, binary);
	return binary;
}

inline void encodeBase64(const unsigned char *bytes, size_t length, std::string &base64) {
	static constexpr const char *base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	while (length--) {
		auto v0 = *(bytes++);
		// top 6 bits of v0
		base64.push_back(base64Chars[v0>>2]);
		
		if (!length--) {
			// bottom 2 bits of v0
			base64.push_back(base64Chars[(v0&0x03)<<4]);
			base64.push_back('=');
			base64.push_back('=');
			break;
		};
		auto v1 = *(bytes++);
		// bottom 2 bits of v0, top 4 bits of v1
		base64.push_back(base64Chars[((v0&0x03)<<4)|(v1>>4)]);

		if (!length--) {
			// bottom 4 bits of v1
			base64.push_back(base64Chars[(v1&0x0F)<<2]);
			base64.push_back('=');
			break;
		};
		auto v2 = *(bytes++);
		// bottom 4 bits of v1, top 2 bits of v2
		base64.push_back(base64Chars[((v1&0x0F)<<2)|(v2>>6)]);
		// bottom 6 bits of v2
		base64.push_back(base64Chars[v2&0x3F]);
	}
}
inline std::string encodeBase64(const unsigned char *bytes, size_t length) {
	std::string base64;
	encodeBase64(bytes, length, base64);
	return base64;
}

inline std::string guessMediaType(const char *path) {
	static const std::unordered_map<std::string, std::pair<const char *, const char *>> extMap{
		{"3g2", {"video", "3gpp2"}},
		{"3gp", {"video", "3gpp"}},
		{"3gpp", {"video", "3gpp"}},
		{"3mf", {"model", "3mf"}},
		{"aac", {"audio", "aac"}},
		{"ac", {"application", "pkix-attr-cert"}},
		{"adp", {"audio", "adpcm"}},
		{"adts", {"audio", "aac"}},
		{"ai", {"application", "postscript"}},
		{"aml", {"application", "automationml-aml+xml"}},
		{"amlx", {"application", "automationml-amlx+zip"}},
		{"amr", {"audio", "amr"}},
		{"apng", {"image", "apng"}},
		{"appcache", {"text", "cache-manifest"}},
		{"appinstaller", {"application", "appinstaller"}},
		{"appx", {"application", "appx"}},
		{"appxbundle", {"application", "appxbundle"}},
		{"asc", {"application", "pgp-signature"}},
		{"atom", {"application", "atom+xml"}},
		{"atomcat", {"application", "atomcat+xml"}},
		{"atomdeleted", {"application", "atomdeleted+xml"}},
		{"atomsvc", {"application", "atomsvc+xml"}},
		{"au", {"audio", "basic"}},
		{"avci", {"image", "avci"}},
		{"avcs", {"image", "avcs"}},
		{"avif", {"image", "avif"}},
		{"aw", {"application", "applixware"}},
		{"bdoc", {"application", "bdoc"}},
		{"bin", {"application", "octet-stream"}},
		{"bmp", {"image", "bmp"}},
		{"bpk", {"application", "octet-stream"}},
		{"buffer", {"application", "octet-stream"}},
		{"ccxml", {"application", "ccxml+xml"}},
		{"cdfx", {"application", "cdfx+xml"}},
		{"cdmia", {"application", "cdmi-capability"}},
		{"cdmic", {"application", "cdmi-container"}},
		{"cdmid", {"application", "cdmi-domain"}},
		{"cdmio", {"application", "cdmi-object"}},
		{"cdmiq", {"application", "cdmi-queue"}},
		{"cer", {"application", "pkix-cert"}},
		{"cgm", {"image", "cgm"}},
		{"cjs", {"application", "node"}},
		{"class", {"application", "java-vm"}},
		{"coffee", {"text", "coffeescript"}},
		{"conf", {"text", "plain"}},
		{"cpl", {"application", "cpl+xml"}},
		{"cpt", {"application", "mac-compactpro"}},
		{"crl", {"application", "pkix-crl"}},
		{"css", {"text", "css"}},
		{"csv", {"text", "csv"}},
		{"cu", {"application", "cu-seeme"}},
		{"cwl", {"application", "cwl"}},
		{"davmount", {"application", "davmount+xml"}},
		{"dbk", {"application", "docbook+xml"}},
		{"deb", {"application", "octet-stream"}},
		{"def", {"text", "plain"}},
		{"deploy", {"application", "octet-stream"}},
		{"dib", {"image", "bmp"}},
		{"disposition-notification", {"message", "disposition-notification"}},
		{"dist", {"application", "octet-stream"}},
		{"distz", {"application", "octet-stream"}},
		{"dll", {"application", "octet-stream"}},
		{"dmg", {"application", "octet-stream"}},
		{"dms", {"application", "octet-stream"}},
		{"doc", {"application", "msword"}},
		{"dot", {"application", "msword"}},
		{"dpx", {"image", "dpx"}},
		{"drle", {"image", "dicom-rle"}},
		{"dssc", {"application", "dssc+der"}},
		{"dtd", {"application", "xml-dtd"}},
		{"dump", {"application", "octet-stream"}},
		{"dwd", {"application", "atsc-dwd+xml"}},
		{"ear", {"application", "java-archive"}},
		{"ecma", {"application", "ecmascript"}},
		{"elc", {"application", "octet-stream"}},
		{"emf", {"image", "emf"}},
		{"eml", {"message", "rfc822"}},
		{"emma", {"application", "emma+xml"}},
		{"emotionml", {"application", "emotionml+xml"}},
		{"eps", {"application", "postscript"}},
		{"epub", {"application", "epub+zip"}},
		{"exe", {"application", "octet-stream"}},
		{"exi", {"application", "exi"}},
		{"exp", {"application", "express"}},
		{"exr", {"image", "aces"}},
		{"ez", {"application", "andrew-inset"}},
		{"fdf", {"application", "fdf"}},
		{"fdt", {"application", "fdt+xml"}},
		{"fits", {"image", "fits"}},
		{"g3", {"image", "g3fax"}},
		{"gbr", {"application", "rpki-ghostbusters"}},
		{"geojson", {"application", "geo+json"}},
		{"gif", {"image", "gif"}},
		{"glb", {"model", "gltf-binary"}},
		{"gltf", {"model", "gltf+json"}},
		{"gml", {"application", "gml+xml"}},
		{"gpx", {"application", "gpx+xml"}},
		{"gram", {"application", "srgs"}},
		{"grxml", {"application", "srgs+xml"}},
		{"gxf", {"application", "gxf"}},
		{"gz", {"application", "gzip"}},
		{"h261", {"video", "h261"}},
		{"h263", {"video", "h263"}},
		{"h264", {"video", "h264"}},
		{"heic", {"image", "heic"}},
		{"heics", {"image", "heic-sequence"}},
		{"heif", {"image", "heif"}},
		{"heifs", {"image", "heif-sequence"}},
		{"hej2", {"image", "hej2k"}},
		{"held", {"application", "atsc-held+xml"}},
		{"hjson", {"application", "hjson"}},
		{"hlp", {"application", "winhlp"}},
		{"hqx", {"application", "mac-binhex40"}},
		{"hsj2", {"image", "hsj2"}},
		{"htm", {"text", "html"}},
		{"html", {"text", "html"}},
		{"ics", {"text", "calendar"}},
		{"ief", {"image", "ief"}},
		{"ifb", {"text", "calendar"}},
		{"iges", {"model", "iges"}},
		{"igs", {"model", "iges"}},
		{"img", {"application", "octet-stream"}},
		{"in", {"text", "plain"}},
		{"ini", {"text", "plain"}},
		{"ink", {"application", "inkml+xml"}},
		{"inkml", {"application", "inkml+xml"}},
		{"ipfix", {"application", "ipfix"}},
		{"iso", {"application", "octet-stream"}},
		{"its", {"application", "its+xml"}},
		{"jade", {"text", "jade"}},
		{"jar", {"application", "java-archive"}},
		{"jhc", {"image", "jphc"}},
		{"jls", {"image", "jls"}},
		{"jp2", {"image", "jp2"}},
		{"jpe", {"image", "jpeg"}},
		{"jpeg", {"image", "jpeg"}},
		{"jpf", {"image", "jpx"}},
		{"jpg", {"image", "jpeg"}},
		{"jpg2", {"image", "jp2"}},
		{"jpgm", {"video", "jpm"}},
		{"jpgv", {"video", "jpeg"}},
		{"jph", {"image", "jph"}},
		{"jpm", {"video", "jpm"}},
		{"jpx", {"image", "jpx"}},
		{"js", {"text", "javascript"}},
		{"json", {"application", "json"}},
		{"json5", {"application", "json5"}},
		{"jsonld", {"application", "ld+json"}},
		{"jsonml", {"application", "jsonml+json"}},
		{"jsx", {"text", "jsx"}},
		{"jt", {"model", "jt"}},
		{"jxl", {"image", "jxl"}},
		{"jxr", {"image", "jxr"}},
		{"jxra", {"image", "jxra"}},
		{"jxrs", {"image", "jxrs"}},
		{"jxs", {"image", "jxs"}},
		{"jxsc", {"image", "jxsc"}},
		{"jxsi", {"image", "jxsi"}},
		{"jxss", {"image", "jxss"}},
		{"kar", {"audio", "midi"}},
		{"ktx", {"image", "ktx"}},
		{"ktx2", {"image", "ktx2"}},
		{"less", {"text", "less"}},
		{"lgr", {"application", "lgr+xml"}},
		{"list", {"text", "plain"}},
		{"litcoffee", {"text", "coffeescript"}},
		{"log", {"text", "plain"}},
		{"lostxml", {"application", "lost+xml"}},
		{"lrf", {"application", "octet-stream"}},
		{"m1v", {"video", "mpeg"}},
		{"m21", {"application", "mp21"}},
		{"m2a", {"audio", "mpeg"}},
		{"m2t", {"video", "mp2t"}},
		{"m2ts", {"video", "mp2t"}},
		{"m2v", {"video", "mpeg"}},
		{"m3a", {"audio", "mpeg"}},
		{"m4a", {"audio", "mp4"}},
		{"m4p", {"application", "mp4"}},
		{"m4s", {"video", "iso.segment"}},
		{"ma", {"application", "mathematica"}},
		{"mads", {"application", "mads+xml"}},
		{"maei", {"application", "mmt-aei+xml"}},
		{"man", {"text", "troff"}},
		{"manifest", {"text", "cache-manifest"}},
		{"map", {"application", "json"}},
		{"mar", {"application", "octet-stream"}},
		{"markdown", {"text", "markdown"}},
		{"mathml", {"application", "mathml+xml"}},
		{"mb", {"application", "mathematica"}},
		{"mbox", {"application", "mbox"}},
		{"md", {"text", "markdown"}},
		{"mdx", {"text", "mdx"}},
		{"me", {"text", "troff"}},
		{"mesh", {"model", "mesh"}},
		{"meta4", {"application", "metalink4+xml"}},
		{"metalink", {"application", "metalink+xml"}},
		{"mets", {"application", "mets+xml"}},
		{"mft", {"application", "rpki-manifest"}},
		{"mid", {"audio", "midi"}},
		{"midi", {"audio", "midi"}},
		{"mime", {"message", "rfc822"}},
		{"mj2", {"video", "mj2"}},
		{"mjp2", {"video", "mj2"}},
		{"mjs", {"text", "javascript"}},
		{"mml", {"text", "mathml"}},
		{"mods", {"application", "mods+xml"}},
		{"mov", {"video", "quicktime"}},
		{"mp2", {"audio", "mpeg"}},
		{"mp21", {"application", "mp21"}},
		{"mp2a", {"audio", "mpeg"}},
		{"mp3", {"audio", "mpeg"}},
		{"mp4", {"video", "mp4"}},
		{"mp4a", {"audio", "mp4"}},
		{"mp4s", {"application", "mp4"}},
		{"mp4v", {"video", "mp4"}},
		{"mpd", {"application", "dash+xml"}},
		{"mpe", {"video", "mpeg"}},
		{"mpeg", {"video", "mpeg"}},
		{"mpf", {"application", "media-policy-dataset+xml"}},
		{"mpg", {"video", "mpeg"}},
		{"mpg4", {"video", "mp4"}},
		{"mpga", {"audio", "mpeg"}},
		{"mpp", {"application", "dash-patch+xml"}},
		{"mrc", {"application", "marc"}},
		{"mrcx", {"application", "marcxml+xml"}},
		{"ms", {"text", "troff"}},
		{"mscml", {"application", "mediaservercontrol+xml"}},
		{"msh", {"model", "mesh"}},
		{"msi", {"application", "octet-stream"}},
		{"msix", {"application", "msix"}},
		{"msixbundle", {"application", "msixbundle"}},
		{"msm", {"application", "octet-stream"}},
		{"msp", {"application", "octet-stream"}},
		{"mtl", {"model", "mtl"}},
		{"mts", {"video", "mp2t"}},
		{"musd", {"application", "mmt-usd+xml"}},
		{"mxf", {"application", "mxf"}},
		{"mxmf", {"audio", "mobile-xmf"}},
		{"mxml", {"application", "xv+xml"}},
		{"n3", {"text", "n3"}},
		{"nb", {"application", "mathematica"}},
		{"nq", {"application", "n-quads"}},
		{"nt", {"application", "n-triples"}},
		{"obj", {"model", "obj"}},
		{"oda", {"application", "oda"}},
		{"oga", {"audio", "ogg"}},
		{"ogg", {"audio", "ogg"}},
		{"ogv", {"video", "ogg"}},
		{"ogx", {"application", "ogg"}},
		{"omdoc", {"application", "omdoc+xml"}},
		{"onepkg", {"application", "onenote"}},
		{"onetmp", {"application", "onenote"}},
		{"onetoc", {"application", "onenote"}},
		{"onetoc2", {"application", "onenote"}},
		{"opf", {"application", "oebps-package+xml"}},
		{"opus", {"audio", "ogg"}},
		{"otf", {"font", "otf"}},
		{"owl", {"application", "rdf+xml"}},
		{"oxps", {"application", "oxps"}},
		{"p10", {"application", "pkcs10"}},
		{"p7c", {"application", "pkcs7-mime"}},
		{"p7m", {"application", "pkcs7-mime"}},
		{"p7s", {"application", "pkcs7-signature"}},
		{"p8", {"application", "pkcs8"}},
		{"pdf", {"application", "pdf"}},
		{"pfr", {"application", "font-tdpfr"}},
		{"pgp", {"application", "pgp-encrypted"}},
		{"pkg", {"application", "octet-stream"}},
		{"pki", {"application", "pkixcmp"}},
		{"pkipath", {"application", "pkix-pkipath"}},
		{"pls", {"application", "pls+xml"}},
		{"png", {"image", "png"}},
		{"prc", {"model", "prc"}},
		{"prf", {"application", "pics-rules"}},
		{"provx", {"application", "provenance+xml"}},
		{"ps", {"application", "postscript"}},
		{"pskcxml", {"application", "pskc+xml"}},
		{"qt", {"video", "quicktime"}},
		{"raml", {"application", "raml+yaml"}},
		{"rapd", {"application", "route-apd+xml"}},
		{"rdf", {"application", "rdf+xml"}},
		{"relo", {"application", "p2p-overlay+xml"}},
		{"rif", {"application", "reginfo+xml"}},
		{"rl", {"application", "resource-lists+xml"}},
		{"rld", {"application", "resource-lists-diff+xml"}},
		{"rmi", {"audio", "midi"}},
		{"rnc", {"application", "relax-ng-compact-syntax"}},
		{"rng", {"application", "xml"}},
		{"roa", {"application", "rpki-roa"}},
		{"roff", {"text", "troff"}},
		{"rq", {"application", "sparql-query"}},
		{"rs", {"application", "rls-services+xml"}},
		{"rsat", {"application", "atsc-rsat+xml"}},
		{"rsd", {"application", "rsd+xml"}},
		{"rsheet", {"application", "urc-ressheet+xml"}},
		{"rss", {"application", "rss+xml"}},
		{"rtf", {"text", "rtf"}},
		{"rtx", {"text", "richtext"}},
		{"rusd", {"application", "route-usd+xml"}},
		{"s3m", {"audio", "s3m"}},
		{"sbml", {"application", "sbml+xml"}},
		{"scq", {"application", "scvp-cv-request"}},
		{"scs", {"application", "scvp-cv-response"}},
		{"sdp", {"application", "sdp"}},
		{"senmlx", {"application", "senml+xml"}},
		{"sensmlx", {"application", "sensml+xml"}},
		{"ser", {"application", "java-serialized-object"}},
		{"setpay", {"application", "set-payment-initiation"}},
		{"setreg", {"application", "set-registration-initiation"}},
		{"sgi", {"image", "sgi"}},
		{"sgm", {"text", "sgml"}},
		{"sgml", {"text", "sgml"}},
		{"shex", {"text", "shex"}},
		{"shf", {"application", "shf+xml"}},
		{"shtml", {"text", "html"}},
		{"sieve", {"application", "sieve"}},
		{"sig", {"application", "pgp-signature"}},
		{"sil", {"audio", "silk"}},
		{"silo", {"model", "mesh"}},
		{"siv", {"application", "sieve"}},
		{"slim", {"text", "slim"}},
		{"slm", {"text", "slim"}},
		{"sls", {"application", "route-s-tsid+xml"}},
		{"smi", {"application", "smil+xml"}},
		{"smil", {"application", "smil+xml"}},
		{"snd", {"audio", "basic"}},
		{"so", {"application", "octet-stream"}},
		{"spdx", {"text", "spdx"}},
		{"spp", {"application", "scvp-vp-response"}},
		{"spq", {"application", "scvp-vp-request"}},
		{"spx", {"audio", "ogg"}},
		{"sql", {"application", "sql"}},
		{"sru", {"application", "sru+xml"}},
		{"srx", {"application", "sparql-results+xml"}},
		{"ssdl", {"application", "ssdl+xml"}},
		{"ssml", {"application", "ssml+xml"}},
		{"stk", {"application", "hyperstudio"}},
		{"stl", {"model", "stl"}},
		{"stpx", {"model", "step+xml"}},
		{"stpxz", {"model", "step-xml+zip"}},
		{"stpz", {"model", "step+zip"}},
		{"styl", {"text", "stylus"}},
		{"stylus", {"text", "stylus"}},
		{"svg", {"image", "svg+xml"}},
		{"svgz", {"image", "svg+xml"}},
		{"swidtag", {"application", "swid+xml"}},
		{"t", {"text", "troff"}},
		{"t38", {"image", "t38"}},
		{"td", {"application", "urc-targetdesc+xml"}},
		{"tei", {"application", "tei+xml"}},
		{"teicorpus", {"application", "tei+xml"}},
		{"text", {"text", "plain"}},
		{"tfi", {"application", "thraud+xml"}},
		{"tfx", {"image", "tiff-fx"}},
		{"tif", {"image", "tiff"}},
		{"tiff", {"image", "tiff"}},
		{"toml", {"application", "toml"}},
		{"tr", {"text", "troff"}},
		{"trig", {"application", "trig"}},
		{"ts", {"video", "mp2t"}},
		{"tsd", {"application", "timestamped-data"}},
		{"tsv", {"text", "tab-separated-values"}},
		{"ttc", {"font", "collection"}},
		{"ttf", {"font", "ttf"}},
		{"ttl", {"text", "turtle"}},
		{"ttml", {"application", "ttml+xml"}},
		{"txt", {"text", "plain"}},
		{"u3d", {"model", "u3d"}},
		{"u8dsn", {"message", "global-delivery-status"}},
		{"u8hdr", {"message", "global-headers"}},
		{"u8mdn", {"message", "global-disposition-notification"}},
		{"u8msg", {"message", "global"}},
		{"ubj", {"application", "ubjson"}},
		{"uri", {"text", "uri-list"}},
		{"uris", {"text", "uri-list"}},
		{"urls", {"text", "uri-list"}},
		{"vcard", {"text", "vcard"}},
		{"vrml", {"model", "vrml"}},
		{"vtt", {"text", "vtt"}},
		{"vxml", {"application", "voicexml+xml"}},
		{"war", {"application", "java-archive"}},
		{"wasm", {"application", "wasm"}},
		{"wav", {"audio", "wave"}},
		{"weba", {"audio", "webm"}},
		{"webm", {"video", "webm"}},
		{"webmanifest", {"application", "manifest+json"}},
		{"webp", {"image", "webp"}},
		{"wgsl", {"text", "wgsl"}},
		{"wgt", {"application", "widget"}},
		{"wif", {"application", "watcherinfo+xml"}},
		{"wmf", {"image", "wmf"}},
		{"woff", {"font", "woff"}},
		{"woff2", {"font", "woff2"}},
		{"wrl", {"model", "vrml"}},
		{"wsdl", {"application", "wsdl+xml"}},
		{"wspolicy", {"application", "wspolicy+xml"}},
		{"x3d", {"model", "x3d+xml"}},
		{"x3db", {"model", "x3d+fastinfoset"}},
		{"x3dbz", {"model", "x3d+binary"}},
		{"x3dv", {"model", "x3d-vrml"}},
		{"x3dvz", {"model", "x3d+vrml"}},
		{"x3dz", {"model", "x3d+xml"}},
		{"xaml", {"application", "xaml+xml"}},
		{"xav", {"application", "xcap-att+xml"}},
		{"xca", {"application", "xcap-caps+xml"}},
		{"xcs", {"application", "calendar+xml"}},
		{"xdf", {"application", "xcap-diff+xml"}},
		{"xdssc", {"application", "dssc+xml"}},
		{"xel", {"application", "xcap-el+xml"}},
		{"xenc", {"application", "xenc+xml"}},
		{"xer", {"application", "patch-ops-error+xml"}},
		{"xfdf", {"application", "xfdf"}},
		{"xht", {"application", "xhtml+xml"}},
		{"xhtml", {"application", "xhtml+xml"}},
		{"xhvml", {"application", "xv+xml"}},
		{"xlf", {"application", "xliff+xml"}},
		{"xm", {"audio", "xm"}},
		{"xml", {"text", "xml"}},
		{"xns", {"application", "xcap-ns+xml"}},
		{"xop", {"application", "xop+xml"}},
		{"xpl", {"application", "xproc+xml"}},
		{"xsd", {"application", "xml"}},
		{"xsl", {"application", "xslt+xml"}},
		{"xslt", {"application", "xslt+xml"}},
		{"xspf", {"application", "xspf+xml"}},
		{"xvm", {"application", "xv+xml"}},
		{"xvml", {"application", "xv+xml"}},
		{"yaml", {"text", "yaml"}},
		{"yang", {"application", "yang"}},
		{"yin", {"application", "yin+xml"}},
		{"yml", {"text", "yaml"}},
		{"zip", {"application", "zip"}}
	};
	size_t posEnd = std::strlen(path);
	for (size_t i = 0; i < posEnd; ++i) {
		auto c = path[i];
		if (c == '?' || c == '#') { // strip query or hash
			posEnd = i;
			break;
		}
	}
	size_t pos = posEnd;
	while (pos > 0) {
		auto c = path[--pos];
		if (c == '.' || c == '/') {
			++pos;
			break;
		}
	}
	std::string ext{path + pos, path + posEnd};
	if (ext.empty()) return "text/html;charset=utf-8";
	for (auto &c : ext) c |= 0x20; // lower-cases alphanumeric ascii
	auto iter = extMap.find(ext);
	if (iter == extMap.end()) return "application/octet-stream";
	auto &pair = iter->second;
	if (!std::strcmp(pair.first, "text")) {
		// Assume all text is UTF-8, because it really should be
		return std::string(pair.first) + "/" + pair.second + ";charset=utf-8";
	}
	return std::string(pair.first) + "/" + pair.second;
}

}} // namespace

