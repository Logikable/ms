#!/bin/sh
# Fetches a research page from a wiki that turns plain scripted requests away.
#
#     tools/wikifetch.sh <url>            # prints the page to stdout
#
# What each source wants, measured 2026-09-05:
#
#   maplestorywiki.net  The PAGE is Cloudflare-challenged again; no header here
#                       beats it. Its API still answers plain curl, at
#                       /api.php -- NOT /w/api.php. Fetch articles that way.
#   namu.wiki           A browser User-Agent with no client hints is refused;
#                       one sec-ch-ua header is enough to be let in. Both
#                       namu.wiki and en.namu.wiki serve the Korean article.
#   strategywiki.org    A Cloudflare JS challenge no header beats. Read it
#                       through web.archive.org instead.
#
# A source that answers 403 has not necessarily walled the door for good --
# re-measure before believing an old note here, this one included.
set -eu

if [ $# -ne 1 ]; then
  echo "usage: $0 <url>" >&2
  exit 2
fi

UA='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like'
UA="$UA Gecko) Chrome/126.0.0.0 Safari/537.36"

exec curl -sS --fail --http2 --compressed --max-time 30 \
  -A "$UA" \
  -H 'Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8' \
  -H 'Accept-Language: en-US,en;q=0.9,ko;q=0.8' \
  -H 'sec-ch-ua: "Chromium";v="126", "Not:A-Brand";v="24"' \
  -H 'sec-ch-ua-platform: "Windows"' \
  -H 'Upgrade-Insecure-Requests: 1' \
  "$1"
