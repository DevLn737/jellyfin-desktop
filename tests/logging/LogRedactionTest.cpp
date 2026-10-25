#include "utils/Log.h"
#include "utils/LogRedaction.h"
#include <cstdlib>
#include <iostream>
#include "LogCensorMethod.inc"

static void check(const QString& input, const QString& expected, const char* scenario)
{
  QString actual = input;
  Log::CensorAuthTokens(actual);
  if (actual != expected || actual.size() != input.size()) {
    // Never print the test's credential-bearing input on a failed assertion.
    std::cerr << "Log redaction failed: " << scenario << '\n';
    std::exit(1);
  }
  Log::CensorAuthTokens(actual);
  if (actual != expected) std::exit(2);
}

int main()
{
  check("url?api_key=abc&start=17", "url?api_key=xxx&start=17", "short token at query boundary");
  check("API_KEY=abc", "API_KEY=xxx", "short token at end of message");
  check("x?ApiKey=abcdef#page", "x?ApiKey=xxxxxx#page", "mixed case and fragment");
  check("AccessToken=abc def", "AccessToken=xxx def", "whitespace boundary");
  check("{\"AccessToken\" : \"short\", \"user\":17}", "{\"AccessToken\" : \"xxxxx\", \"user\":17}", "spaced JSON");
  check(R"({\"AccessToken\":\"short\"})", R"({\"AccessToken\":\"xxxxx\"})", "JSON escaped in another log string");
  check("X-MediaBrowser-Token%3dabc%26Start=17", "X-MediaBrowser-Token%3dxxx%26Start=17", "encoded separator and boundary");
  check("%22AccessToken%22%3A%22abc%22", "%22AccessToken%22%3A%22xxx%22", "encoded JSON quotes");
  check("stream%3Fapi_key%3Dabc%26Start%3D17", "stream%3Fapi_key%3Dxxx%26Start%3D17", "encoded query delimiter before key");
  check("Authorization: Bearer abc.def-123_+=/", "Authorization: Bearer xxxxxxxxxxxxxxx", "bearer opaque characters");
  check("{\"Authorization\":\"bearer short\"}", "{\"Authorization\":\"bearer xxxxx\"}", "bearer in JSON header");
  check("Authorization%3ABearer%20secret%0d%0anext=1", "Authorization%3ABearer%20xxxxxx%0d%0anext=1", "encoded bearer header");
  check("Authorization: MediaBrowser Client=\"Desktop\", Token=\"short\", Version=\"2\"",
        "Authorization: MediaBrowser Client=\"Desktop\", Token=\"xxxxx\", Version=\"2\"", "MediaBrowser credential and useful metadata");
  check("MediaBrowser TOKEN=short\nToken=useful", "MediaBrowser TOKEN=xxxxx\nToken=useful", "header scope does not cross lines");
  check("Emby Token='short'", "Emby Token='xxxxx'", "Emby header");
  check("MediaBrowser%20Client%3DDesktop%2CToken%3Dsecret%2CVersion%3D2", "MediaBrowser%20Client%3DDesktop%2CToken%3Dxxxxxx%2CVersion%3D2", "encoded MediaBrowser fields");
  check("api_key=&ApiKey=ok", "api_key=&ApiKey=xx", "empty credential followed by another");
  check("api_key=\"\" next=1", "api_key=\"\" next=1", "empty quoted credential");
  check("notapi_key=useful Token=lexeme AccessTokenCount=17", "notapi_key=useful Token=lexeme AccessTokenCount=17", "unrelated fields unchanged");
  const QString longToken(4096, QLatin1Char('a'));
  check("url?api_key=" + longToken + "&stream=2", "url?api_key=" + QString(4096, QLatin1Char('x')) + "&stream=2", "long token suffix fully removed");
  const QString malformed = QStringLiteral("MediaBrowser ").repeated(20000);
  check(malformed, malformed, "large malformed header remains bounded");
  std::cout << "Production known-credential log redaction passed\n";
}
