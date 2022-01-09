// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_util.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/check.h"
#include "base/cxx17_backports.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/stl_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "net/base/features.h"
#include "net/base/isolation_info.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "net/cookies/cookie_access_delegate.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_monster.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/first_party_set_metadata.h"
#include "net/cookies/same_party_context.h"
#include "net/http/http_util.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace net {
namespace cookie_util {

namespace {

using ContextType = CookieOptions::SameSiteCookieContext::ContextType;
using ContextMetadata = CookieOptions::SameSiteCookieContext::ContextMetadata;

base::Time MinNonNullTime() {
  return base::Time::FromInternalValue(1);
}

// Tries to assemble a base::Time given a base::Time::Exploded representing a
// UTC calendar date.
//
// If the date falls outside of the range supported internally by
// FromUTCExploded() on the current platform, then the result is:
//
// * Time(1) if it's below the range FromUTCExploded() supports.
// * Time::Max() if it's above the range FromUTCExploded() supports.
bool SaturatedTimeFromUTCExploded(const base::Time::Exploded& exploded,
                                  base::Time* out) {
  // Try to calculate the base::Time in the normal fashion.
  if (base::Time::FromUTCExploded(exploded, out)) {
    // Don't return Time(0) on success.
    if (out->is_null())
      *out = MinNonNullTime();
    return true;
  }

  // base::Time::FromUTCExploded() has platform-specific limits:
  //
  // * Windows: Years 1601 - 30827
  // * 32-bit POSIX: Years 1970 - 2038
  //
  // Work around this by returning min/max valid times for times outside those
  // ranges when imploding the time is doomed to fail.
  //
  // Note that the following implementation is NOT perfect. It will accept
  // some invalid calendar dates in the out-of-range case.
  if (!exploded.HasValidValues())
    return false;

  if (exploded.year > base::Time::kExplodedMaxYear) {
    *out = base::Time::Max();
    return true;
  }
  if (exploded.year < base::Time::kExplodedMinYear) {
    *out = MinNonNullTime();
    return true;
  }

  return false;
}

struct ComputeSameSiteContextResult {
  ContextType context_type = ContextType::CROSS_SITE;
  ContextMetadata metadata;
};

CookieOptions::SameSiteCookieContext MakeSameSiteCookieContext(
    const ComputeSameSiteContextResult& result,
    const ComputeSameSiteContextResult& schemeful_result) {
  return CookieOptions::SameSiteCookieContext(
      result.context_type, schemeful_result.context_type, result.metadata,
      schemeful_result.metadata);
}

// This function consolidates the common logic for computing SameSite cookie
// access context in various situations (HTTP vs JS; get vs set).
//
// `is_http` is whether the current cookie access request is associated with a
// network request (as opposed to a non-HTTP API, i.e., JavaScript).
//
// `compute_schemefully` is whether the current computation is for a
// schemeful_context, i.e. whether scheme should be considered when comparing
// two sites.
//
// See documentation of `ComputeSameSiteContextForRequest` for explanations of
// other parameters.
ComputeSameSiteContextResult ComputeSameSiteContext(
    const std::vector<GURL>& url_chain,
    const SiteForCookies& site_for_cookies,
    const absl::optional<url::Origin>& initiator,
    bool is_http,
    bool is_main_frame_navigation,
    bool compute_schemefully) {
  DCHECK(!url_chain.empty());
  const GURL& request_url = url_chain.back();
  const auto is_same_site_with_site_for_cookies =
      [&site_for_cookies, compute_schemefully](const GURL& url) {
        return site_for_cookies.IsFirstPartyWithSchemefulMode(
            url, compute_schemefully);
      };

  bool site_for_cookies_is_same_site =
      is_same_site_with_site_for_cookies(request_url);

  // If the request is a main frame navigation, site_for_cookies must either be
  // null (for opaque origins, e.g., data: origins) or same-site with the
  // request URL (both schemefully and schemelessly), and the URL cannot be
  // ws/wss (these schemes are not navigable).
  DCHECK(!is_main_frame_navigation || site_for_cookies_is_same_site ||
         site_for_cookies.IsNull());
  DCHECK(!is_main_frame_navigation || !request_url.SchemeIsWSOrWSS());

  // Defaults to a cross-site context type.
  ComputeSameSiteContextResult result;

  if (!site_for_cookies_is_same_site)
    return result;

  // Create a SiteForCookies object from the initiator so that we can reuse
  // IsFirstPartyWithSchemefulMode().
  bool same_site_initiator =
      !initiator ||
      SiteForCookies::FromOrigin(initiator.value())
          .IsFirstPartyWithSchemefulMode(request_url, compute_schemefully);

  // Check that the URLs in the redirect chain are all same-site with the
  // site_for_cookies and hence (by transitivity) same-site with the request
  // URL. (If the URL chain only has one member, it's the request_url and we've
  // already checked it previously.)
  bool same_site_redirect_chain =
      url_chain.size() == 1u ||
      base::ranges::all_of(url_chain, is_same_site_with_site_for_cookies);

  // Whether the context would be SAME_SITE_STRICT if not considering redirect
  // chains, but is different after considering redirect chains.
  bool cross_site_redirect_downgraded_from_strict = false;
  // Allows the kCookieSameSiteConsidersRedirectChain feature to override the
  // result and use SAME_SITE_STRICT.
  bool use_strict = false;

  if (same_site_initiator) {
    if (same_site_redirect_chain) {
      result.context_type = ContextType::SAME_SITE_STRICT;
      return result;
    }
    cross_site_redirect_downgraded_from_strict = true;
    // If we are not supposed to consider redirect chains, record that the
    // context result should ultimately be strictly same-site. We cannot
    // just return early from here because we don't yet know what the context
    // gets downgraded to, so we can't return with the correct metadata until we
    // go through the rest of the logic below to determine that.
    use_strict = !base::FeatureList::IsEnabled(
        features::kCookieSameSiteConsidersRedirectChain);
  }

  if (!is_http || is_main_frame_navigation) {
    if (cross_site_redirect_downgraded_from_strict) {
      result.metadata.cross_site_redirect_downgrade =
          ContextMetadata::ContextDowngradeType::kStrictToLax;
    }
    result.context_type =
        use_strict ? ContextType::SAME_SITE_STRICT : ContextType::SAME_SITE_LAX;
    return result;
  }

  if (cross_site_redirect_downgraded_from_strict) {
    result.metadata.cross_site_redirect_downgrade =
        ContextMetadata::ContextDowngradeType::kStrictToCross;
  }
  result.context_type =
      use_strict ? ContextType::SAME_SITE_STRICT : ContextType::CROSS_SITE;

  return result;
}

// Setting any SameSite={Strict,Lax} cookie only requires a LAX context, so
// normalize any strictly same-site contexts to Lax for cookie writes.
void NormalizeStrictToLaxForSet(ComputeSameSiteContextResult& result) {
  if (result.context_type == ContextType::SAME_SITE_STRICT)
    result.context_type = ContextType::SAME_SITE_LAX;

  switch (result.metadata.cross_site_redirect_downgrade) {
    case ContextMetadata::ContextDowngradeType::kStrictToLax:
      result.metadata.cross_site_redirect_downgrade =
          ContextMetadata::ContextDowngradeType::kNoDowngrade;
      break;
    case ContextMetadata::ContextDowngradeType::kStrictToCross:
      result.metadata.cross_site_redirect_downgrade =
          ContextMetadata::ContextDowngradeType::kLaxToCross;
      break;
    default:
      break;
  }
}

CookieOptions::SameSiteCookieContext ComputeSameSiteContextForSet(
    const std::vector<GURL>& url_chain,
    const SiteForCookies& site_for_cookies,
    const absl::optional<url::Origin>& initiator,
    bool is_http,
    bool is_main_frame_navigation) {
  CookieOptions::SameSiteCookieContext same_site_context;

  ComputeSameSiteContextResult result = ComputeSameSiteContext(
      url_chain, site_for_cookies, initiator, is_http, is_main_frame_navigation,
      false /* compute_schemefully */);
  ComputeSameSiteContextResult schemeful_result = ComputeSameSiteContext(
      url_chain, site_for_cookies, initiator, is_http, is_main_frame_navigation,
      true /* compute_schemefully */);

  NormalizeStrictToLaxForSet(result);
  NormalizeStrictToLaxForSet(schemeful_result);

  return MakeSameSiteCookieContext(result, schemeful_result);
}

bool CookieWithAccessResultSorter(const CookieWithAccessResult& a,
                                  const CookieWithAccessResult& b) {
  return CookieMonster::CookieSorter(&a.cookie, &b.cookie);
}

}  // namespace

void FireStorageAccessHistogram(StorageAccessResult result) {
  UMA_HISTOGRAM_ENUMERATION("API.StorageAccess.AllowedRequests2", result);
}

bool DomainIsHostOnly(const std::string& domain_string) {
  return (domain_string.empty() || domain_string[0] != '.');
}

std::string CookieDomainAsHost(const std::string& cookie_domain) {
  if (DomainIsHostOnly(cookie_domain))
    return cookie_domain;
  return cookie_domain.substr(1);
}

std::string GetEffectiveDomain(const std::string& scheme,
                               const std::string& host) {
  if (scheme == "http" || scheme == "https" || scheme == "ws" ||
      scheme == "wss") {
    return registry_controlled_domains::GetDomainAndRegistry(
        host,
        registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  }

  return CookieDomainAsHost(host);
}

bool GetCookieDomainWithString(const GURL& url,
                               const std::string& domain_string,
                               std::string* result) {
  const std::string url_host(url.host());

  url::CanonHostInfo ignored;
  std::string cookie_domain(CanonicalizeHost(domain_string, &ignored));

  // If no domain was specified in the domain string, default to a host cookie.
  // We match IE/Firefox in allowing a domain=IPADDR if it matches the url
  // ip address hostname exactly.  It should be treated as a host cookie.
  if (domain_string.empty() ||
      (url.HostIsIPAddress() && url_host == cookie_domain)) {
    *result = url_host;
    DCHECK(DomainIsHostOnly(*result));
    return true;
  }

  // Disallow domain names with %-escaped characters.
  for (char c : domain_string) {
    if (c == '%')
      return false;
  }

  // Get the normalized domain specified in cookie line.
  if (cookie_domain.empty())
    return false;
  if (cookie_domain[0] != '.')
    cookie_domain = "." + cookie_domain;

  // Ensure |url| and |cookie_domain| have the same domain+registry.
  const std::string url_scheme(url.scheme());
  const std::string url_domain_and_registry(
      GetEffectiveDomain(url_scheme, url_host));
  if (url_domain_and_registry.empty()) {
    // We match IE/Firefox by treating an exact match between the domain
    // attribute and the request host to be treated as a host cookie.
    if (url_host == domain_string) {
      *result = url_host;
      DCHECK(DomainIsHostOnly(*result));
      return true;
    }

    // Otherwise, IP addresses/intranet hosts/public suffixes can't set
    // domain cookies.
    return false;
  }
  const std::string cookie_domain_and_registry(
      GetEffectiveDomain(url_scheme, cookie_domain));
  if (url_domain_and_registry != cookie_domain_and_registry)
    return false;  // Can't set a cookie on a different domain + registry.

  // Ensure |url_host| is |cookie_domain| or one of its subdomains.  Given that
  // we know the domain+registry are the same from the above checks, this is
  // basically a simple string suffix check.
  const bool is_suffix = (url_host.length() < cookie_domain.length()) ?
      (cookie_domain != ("." + url_host)) :
      (url_host.compare(url_host.length() - cookie_domain.length(),
                        cookie_domain.length(), cookie_domain) != 0);
  if (is_suffix)
    return false;

  *result = cookie_domain;
  return true;
}

// Parse a cookie expiration time.  We try to be lenient, but we need to
// assume some order to distinguish the fields.  The basic rules:
//  - The month name must be present and prefix the first 3 letters of the
//    full month name (jan for January, jun for June).
//  - If the year is <= 2 digits, it must occur after the day of month.
//  - The time must be of the format hh:mm:ss.
// An average cookie expiration will look something like this:
//   Sat, 15-Apr-17 21:01:22 GMT
base::Time ParseCookieExpirationTime(const std::string& time_string) {
  static const char* const kMonths[] = {
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec" };
  // We want to be pretty liberal, and support most non-ascii and non-digit
  // characters as a delimiter.  We can't treat : as a delimiter, because it
  // is the delimiter for hh:mm:ss, and we want to keep this field together.
  // We make sure to include - and +, since they could prefix numbers.
  // If the cookie attribute came in in quotes (ex expires="XXX"), the quotes
  // will be preserved, and we will get them here.  So we make sure to include
  // quote characters, and also \ for anything that was internally escaped.
  static const char kDelimiters[] = "\t !\"#$%&'()*+,-./;<=>?@[\\]^_`{|}~";

  base::Time::Exploded exploded = {0};

  base::StringTokenizer tokenizer(time_string, kDelimiters);

  bool found_day_of_month = false;
  bool found_month = false;
  bool found_time = false;
  bool found_year = false;

  while (tokenizer.GetNext()) {
    const std::string token = tokenizer.token();
    DCHECK(!token.empty());
    bool numerical = base::IsAsciiDigit(token[0]);

    // String field
    if (!numerical) {
      if (!found_month) {
        for (size_t i = 0; i < base::size(kMonths); ++i) {
          // Match prefix, so we could match January, etc
          if (base::StartsWith(token, base::StringPiece(kMonths[i], 3),
                               base::CompareCase::INSENSITIVE_ASCII)) {
            exploded.month = static_cast<int>(i) + 1;
            found_month = true;
            break;
          }
        }
      } else {
        // If we've gotten here, it means we've already found and parsed our
        // month, and we have another string, which we would expect to be the
        // the time zone name.  According to the RFC and my experiments with
        // how sites format their expirations, we don't have much of a reason
        // to support timezones.  We don't want to ever barf on user input,
        // but this DCHECK should pass for well-formed data.
        // DCHECK(token == "GMT");
      }
    // Numeric field w/ a colon
    } else if (token.find(':') != std::string::npos) {
      if (!found_time &&
#ifdef COMPILER_MSVC
          sscanf_s(
#else
          sscanf(
#endif
                 token.c_str(), "%2u:%2u:%2u", &exploded.hour,
                 &exploded.minute, &exploded.second) == 3) {
        found_time = true;
      } else {
        // We should only ever encounter one time-like thing.  If we're here,
        // it means we've found a second, which shouldn't happen.  We keep
        // the first.  This check should be ok for well-formed input:
        // NOTREACHED();
      }
    // Numeric field
    } else {
      // Overflow with atoi() is unspecified, so we enforce a max length.
      if (!found_day_of_month && token.length() <= 2) {
        exploded.day_of_month = atoi(token.c_str());
        found_day_of_month = true;
      } else if (!found_year && token.length() <= 5) {
        exploded.year = atoi(token.c_str());
        found_year = true;
      } else {
        // If we're here, it means we've either found an extra numeric field,
        // or a numeric field which was too long.  For well-formed input, the
        // following check would be reasonable:
        // NOTREACHED();
      }
    }
  }

  if (!found_day_of_month || !found_month || !found_time || !found_year) {
    // We didn't find all of the fields we need.  For well-formed input, the
    // following check would be reasonable:
    // NOTREACHED() << "Cookie parse expiration failed: " << time_string;
    return base::Time();
  }

  // Normalize the year to expand abbreviated years to the full year.
  if (exploded.year >= 70 && exploded.year <= 99)
    exploded.year += 1900;
  if (exploded.year >= 0 && exploded.year <= 69)
    exploded.year += 2000;

  // Note that clipping the date if it is outside of a platform-specific range
  // is permitted by: https://tools.ietf.org/html/rfc6265#section-5.2.1
  base::Time result;
  if (SaturatedTimeFromUTCExploded(exploded, &result))
    return result;

  // One of our values was out of expected range.  For well-formed input,
  // the following check would be reasonable:
  // NOTREACHED() << "Cookie exploded expiration failed: " << time_string;

  return base::Time();
}

GURL CookieDomainAndPathToURL(const std::string& domain,
                              const std::string& path,
                              const std::string& source_scheme) {
  // Note: domain_no_dot could be empty for e.g. file cookies.
  std::string domain_no_dot = CookieDomainAsHost(domain);
  if (domain_no_dot.empty() || source_scheme.empty())
    return GURL();
  return GURL(base::StrCat(
      {source_scheme, url::kStandardSchemeSeparator, domain_no_dot, path}));
}

GURL CookieDomainAndPathToURL(const std::string& domain,
                              const std::string& path,
                              bool is_https) {
  return CookieDomainAndPathToURL(
      domain, path,
      std::string(is_https ? url::kHttpsScheme : url::kHttpScheme));
}

GURL CookieDomainAndPathToURL(const std::string& domain,
                              const std::string& path,
                              CookieSourceScheme source_scheme) {
  return CookieDomainAndPathToURL(domain, path,
                                  source_scheme == CookieSourceScheme::kSecure);
}

GURL CookieOriginToURL(const std::string& domain, bool is_https) {
  return CookieDomainAndPathToURL(domain, "/", is_https);
}

GURL SimulatedCookieSource(const CanonicalCookie& cookie,
                           const std::string& source_scheme) {
  return CookieDomainAndPathToURL(cookie.Domain(), cookie.Path(),
                                  source_scheme);
}

CookieAccessScheme ProvisionalAccessScheme(const GURL& source_url) {
  return source_url.SchemeIsCryptographic()
             ? CookieAccessScheme::kCryptographic
             : IsLocalhost(source_url) ? CookieAccessScheme::kTrustworthy
                                       : CookieAccessScheme::kNonCryptographic;
}

bool IsDomainMatch(const std::string& domain, const std::string& host) {
  // Can domain match in two ways; as a domain cookie (where the cookie
  // domain begins with ".") or as a host cookie (where it doesn't).

  // Some consumers of the CookieMonster expect to set cookies on
  // URLs like http://.strange.url.  To retrieve cookies in this instance,
  // we allow matching as a host cookie even when the domain_ starts with
  // a period.
  if (host == domain)
    return true;

  // Domain cookie must have an initial ".".  To match, it must be
  // equal to url's host with initial period removed, or a suffix of
  // it.

  // Arguably this should only apply to "http" or "https" cookies, but
  // extension cookie tests currently use the funtionality, and if we
  // ever decide to implement that it should be done by preventing
  // such cookies from being set.
  if (domain.empty() || domain[0] != '.')
    return false;

  // The host with a "." prefixed.
  if (domain.compare(1, std::string::npos, host) == 0)
    return true;

  // A pure suffix of the host (ok since we know the domain already
  // starts with a ".")
  return (host.length() > domain.length() &&
          host.compare(host.length() - domain.length(), domain.length(),
                       domain) == 0);
}

void ParseRequestCookieLine(const std::string& header_value,
                            ParsedRequestCookies* parsed_cookies) {
  std::string::const_iterator i = header_value.begin();
  while (i != header_value.end()) {
    // Here we are at the beginning of a cookie.

    // Eat whitespace.
    while (i != header_value.end() && *i == ' ') ++i;
    if (i == header_value.end()) return;

    // Find cookie name.
    std::string::const_iterator cookie_name_beginning = i;
    while (i != header_value.end() && *i != '=') ++i;
    auto cookie_name = base::MakeStringPiece(cookie_name_beginning, i);

    // Find cookie value.
    base::StringPiece cookie_value;
    // Cookies may have no value, in this case '=' may or may not be there.
    if (i != header_value.end() && i + 1 != header_value.end()) {
      ++i;  // Skip '='.
      std::string::const_iterator cookie_value_beginning = i;
      if (*i == '"') {
        ++i;  // Skip '"'.
        while (i != header_value.end() && *i != '"') ++i;
        if (i == header_value.end()) return;
        ++i;  // Skip '"'.
        cookie_value = base::MakeStringPiece(cookie_value_beginning, i);
        // i points to character after '"', potentially a ';'.
      } else {
        while (i != header_value.end() && *i != ';') ++i;
        cookie_value = base::MakeStringPiece(cookie_value_beginning, i);
        // i points to ';' or end of string.
      }
    }
    parsed_cookies->emplace_back(std::string(cookie_name),
                                 std::string(cookie_value));
    // Eat ';'.
    if (i != header_value.end()) ++i;
  }
}

std::string SerializeRequestCookieLine(
    const ParsedRequestCookies& parsed_cookies) {
  std::string buffer;
  for (auto i = parsed_cookies.begin(); i != parsed_cookies.end(); ++i) {
    if (!buffer.empty())
      buffer.append("; ");
    buffer.append(i->first.begin(), i->first.end());
    buffer.push_back('=');
    buffer.append(i->second.begin(), i->second.end());
  }
  return buffer;
}

CookieOptions::SameSiteCookieContext ComputeSameSiteContextForRequest(
    const std::string& http_method,
    const std::vector<GURL>& url_chain,
    const SiteForCookies& site_for_cookies,
    const absl::optional<url::Origin>& initiator,
    bool is_main_frame_navigation,
    bool force_ignore_site_for_cookies) {
  // Set SameSiteCookieContext according to the rules laid out in
  // https://tools.ietf.org/html/draft-ietf-httpbis-rfc6265bis:
  //
  // * Include both "strict" and "lax" same-site cookies if the request's
  //   |url|, |initiator|, and |site_for_cookies| all have the same
  //   registrable domain. Note: this also covers the case of a request
  //   without an initiator (only happens for browser-initiated main frame
  //   navigations). If computing schemefully, the schemes must also match.
  //
  // * Include only "lax" same-site cookies if the request's |URL| and
  //   |site_for_cookies| have the same registrable domain, _and_ the
  //   request's |http_method| is "safe" ("GET" or "HEAD"), and the request
  //   is a main frame navigation.
  //
  //   This case should occur only for cross-site requests which
  //   target a top-level browsing context, with a "safe" method.
  //
  // * Include both "strict" and "lax" same-site cookies if the request is
  //   tagged with a flag allowing it.
  //
  //   Note that this can be the case for requests initiated by extensions,
  //   which need to behave as though they are made by the document itself,
  //   but appear like cross-site ones.
  //
  // * Otherwise, do not include same-site cookies.

  if (force_ignore_site_for_cookies)
    return CookieOptions::SameSiteCookieContext::MakeInclusive();

  ComputeSameSiteContextResult result = ComputeSameSiteContext(
      url_chain, site_for_cookies, initiator, true /* is_http */,
      is_main_frame_navigation, false /* compute_schemefully */);
  ComputeSameSiteContextResult schemeful_result = ComputeSameSiteContext(
      url_chain, site_for_cookies, initiator, true /* is_http */,
      is_main_frame_navigation, true /* compute_schemefully */);

  // If the method is safe, the context is Lax. Otherwise, make a note that
  // the method is unsafe.
  if (!net::HttpUtil::IsMethodSafe(http_method)) {
    if (result.context_type == ContextType::SAME_SITE_LAX)
      result.context_type = ContextType::SAME_SITE_LAX_METHOD_UNSAFE;
    if (schemeful_result.context_type == ContextType::SAME_SITE_LAX)
      schemeful_result.context_type = ContextType::SAME_SITE_LAX_METHOD_UNSAFE;
  }

  return MakeSameSiteCookieContext(result, schemeful_result);
}

NET_EXPORT CookieOptions::SameSiteCookieContext
ComputeSameSiteContextForScriptGet(const GURL& url,
                                   const SiteForCookies& site_for_cookies,
                                   const absl::optional<url::Origin>& initiator,
                                   bool force_ignore_site_for_cookies) {
  if (force_ignore_site_for_cookies)
    return CookieOptions::SameSiteCookieContext::MakeInclusive();

  // We don't check the redirect chain for script access to cookies (only the
  // URL itself).
  ComputeSameSiteContextResult result = ComputeSameSiteContext(
      {url}, site_for_cookies, initiator, false /* is_http */,
      false /* is_main_frame_navigation */, false /* compute_schemefully */);
  ComputeSameSiteContextResult schemeful_result = ComputeSameSiteContext(
      {url}, site_for_cookies, initiator, false /* is_http */,
      false /* is_main_frame_navigation */, true /* compute_schemefully */);

  return MakeSameSiteCookieContext(result, schemeful_result);
}

CookieOptions::SameSiteCookieContext ComputeSameSiteContextForResponse(
    const std::vector<GURL>& url_chain,
    const SiteForCookies& site_for_cookies,
    const absl::optional<url::Origin>& initiator,
    bool is_main_frame_navigation,
    bool force_ignore_site_for_cookies) {
  if (force_ignore_site_for_cookies)
    return CookieOptions::SameSiteCookieContext::MakeInclusiveForSet();

  DCHECK(!url_chain.empty());
  if (is_main_frame_navigation && !site_for_cookies.IsNull()) {
    // If the request is a main frame navigation, site_for_cookies must either
    // be null (for opaque origins, e.g., data: origins) or same-site with the
    // request URL (both schemefully and schemelessly), and the URL cannot be
    // ws/wss (these schemes are not navigable).
    DCHECK(
        site_for_cookies.IsFirstPartyWithSchemefulMode(url_chain.back(), true));
    DCHECK(!url_chain.back().SchemeIsWSOrWSS());
    return CookieOptions::SameSiteCookieContext::MakeInclusiveForSet();
  }

  return ComputeSameSiteContextForSet(url_chain, site_for_cookies, initiator,
                                      true /* is_http */,
                                      is_main_frame_navigation);
}

CookieOptions::SameSiteCookieContext ComputeSameSiteContextForScriptSet(
    const GURL& url,
    const SiteForCookies& site_for_cookies,
    bool force_ignore_site_for_cookies) {
  if (force_ignore_site_for_cookies)
    return CookieOptions::SameSiteCookieContext::MakeInclusiveForSet();

  // It doesn't matter what initiator origin we pass here. Either way, the
  // context will be considered same-site iff the site_for_cookies is same-site
  // with the url. We don't check the redirect chain for script access to
  // cookies (only the URL itself).
  return ComputeSameSiteContextForSet(
      {url}, site_for_cookies, absl::nullopt /* initiator */,
      false /* is_http */, false /* is_main_frame_navigation */);
}

CookieOptions::SameSiteCookieContext ComputeSameSiteContextForSubresource(
    const GURL& url,
    const SiteForCookies& site_for_cookies,
    bool force_ignore_site_for_cookies) {
  if (force_ignore_site_for_cookies)
    return CookieOptions::SameSiteCookieContext::MakeInclusive();

  // If the URL is same-site as site_for_cookies it's same-site as all frames
  // in the tree from the initiator frame up --- including the initiator frame.

  // Schemeless check
  if (!site_for_cookies.IsFirstPartyWithSchemefulMode(url, false)) {
    return CookieOptions::SameSiteCookieContext(ContextType::CROSS_SITE,
                                                ContextType::CROSS_SITE);
  }

  // Schemeful check
  if (!site_for_cookies.IsFirstPartyWithSchemefulMode(url, true)) {
    return CookieOptions::SameSiteCookieContext(ContextType::SAME_SITE_STRICT,
                                                ContextType::CROSS_SITE);
  }

  return CookieOptions::SameSiteCookieContext::MakeInclusive();
}

bool IsSchemefulSameSiteEnabled() {
  return base::FeatureList::IsEnabled(features::kSchemefulSameSite);
}

bool IsFirstPartySetsEnabled() {
  return base::FeatureList::IsEnabled(features::kFirstPartySets);
}

// Returns First-Party Set metadata for the given context. Returns empty/default
// metadata if `isolation_info` is not fully populated, or
// `isolation_info.party_context` is nullopt.
FirstPartySetMetadata ComputeFirstPartySetMetadata(
    const SchemefulSite& request_site,
    const IsolationInfo& isolation_info,
    const CookieAccessDelegate* cookie_access_delegate,
    bool force_ignore_top_frame_party) {
  if (!isolation_info.IsEmpty() && isolation_info.party_context().has_value() &&
      cookie_access_delegate) {
    return cookie_access_delegate->ComputeFirstPartySetMetadata(
        request_site,
        force_ignore_top_frame_party
            ? nullptr
            : base::OptionalOrNullptr(
                  isolation_info.network_isolation_key().GetTopFrameSite()),
        isolation_info.party_context().value());
  }

  return FirstPartySetMetadata();
}

CookieSamePartyStatus GetSamePartyStatus(const CanonicalCookie& cookie,
                                         const CookieOptions& options) {
  if (!IsFirstPartySetsEnabled() || !cookie.IsSameParty() ||
      !options.is_in_nontrivial_first_party_set()) {
    return CookieSamePartyStatus::kNoSamePartyEnforcement;
  }

  switch (options.same_party_context().context_type()) {
    case SamePartyContext::Type::kCrossParty:
      return CookieSamePartyStatus::kEnforceSamePartyExclude;
    case SamePartyContext::Type::kSameParty:
      return CookieSamePartyStatus::kEnforceSamePartyInclude;
  };
}

base::OnceCallback<void(CookieAccessResult)> AdaptCookieAccessResultToBool(
    base::OnceCallback<void(bool)> callback) {
  return base::BindOnce(
      [](base::OnceCallback<void(bool)> inner_callback,
         const CookieAccessResult access_result) {
        bool success = access_result.status.IsInclude();
        std::move(inner_callback).Run(success);
      },
      std::move(callback));
}

CookieList StripAccessResults(
    const CookieAccessResultList& cookie_access_results_list) {
  CookieList cookies;
  for (const CookieWithAccessResult& cookie_with_access_result :
       cookie_access_results_list) {
    cookies.push_back(cookie_with_access_result.cookie);
  }
  return cookies;
}

NET_EXPORT void RecordCookiePortOmniboxHistograms(const GURL& url) {
  int port = url.EffectiveIntPort();

  if (port == url::PORT_UNSPECIFIED)
    return;

  if (IsLocalhost(url)) {
    UMA_HISTOGRAM_ENUMERATION("Cookie.Port.OmniboxURLNavigation.Localhost",
                              ReducePortRangeForCookieHistogram(port));
  } else {
    UMA_HISTOGRAM_ENUMERATION("Cookie.Port.OmniboxURLNavigation.RemoteHost",
                              ReducePortRangeForCookieHistogram(port));
  }
}

NET_EXPORT void DCheckIncludedAndExcludedCookieLists(
    const CookieAccessResultList& included_cookies,
    const CookieAccessResultList& excluded_cookies) {
  // Check that all elements of `included_cookies` really should be included,
  // and that all elements of `excluded_cookies` really should be excluded.
  DCHECK(base::ranges::all_of(included_cookies,
                              [](const net::CookieWithAccessResult& cookie) {
                                return cookie.access_result.status.IsInclude();
                              }));
  DCHECK(base::ranges::none_of(excluded_cookies,
                               [](const net::CookieWithAccessResult& cookie) {
                                 return cookie.access_result.status.IsInclude();
                               }));

  // Check that the included cookies are still in the correct order.
  DCHECK(
      base::ranges::is_sorted(included_cookies, CookieWithAccessResultSorter));
}

}  // namespace cookie_util
}  // namespace net