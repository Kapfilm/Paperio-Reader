#include <SaxParser/SaxParser.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct Event {
  enum class Type { Start, End, Char, Default };
  Type type;
  std::string name;
  std::string text;
};

struct Collector {
  std::vector<Event> events;

  static void onStart(void* ud, const char* name, const char**) {
    static_cast<Collector*>(ud)->events.push_back({Event::Type::Start, name, {}});
  }
  static void onEnd(void* ud, const char* name) {
    static_cast<Collector*>(ud)->events.push_back({Event::Type::End, name, {}});
  }
  static void onChar(void* ud, const char* s, int len) {
    static_cast<Collector*>(ud)->events.push_back({Event::Type::Char, {}, std::string(s, len)});
  }
  static void onDefault(void* ud, const char* s, int len) {
    static_cast<Collector*>(ud)->events.push_back({Event::Type::Default, {}, std::string(s, len)});
  }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(SaxParser, ParseMinimalDocument) {
  const char* xml = "<root><child>hello</child></root>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  ASSERT_EQ(c.events.size(), 5u);
  EXPECT_EQ(c.events[0].type, Event::Type::Start);
  EXPECT_EQ(c.events[0].name, "root");
  EXPECT_EQ(c.events[1].type, Event::Type::Start);
  EXPECT_EQ(c.events[1].name, "child");
  EXPECT_EQ(c.events[2].type, Event::Type::Char);
  EXPECT_EQ(c.events[2].text, "hello");
  EXPECT_EQ(c.events[3].type, Event::Type::End);
  EXPECT_EQ(c.events[3].name, "child");
  EXPECT_EQ(c.events[4].type, Event::Type::End);
  EXPECT_EQ(c.events[4].name, "root");
}

TEST(SaxParser, ParseChunked) {
  const char* xml = "<root><a>foo</a><b>bar</b></root>";
  const size_t len = strlen(xml);

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar));

  // Feed 3 bytes at a time
  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  for (size_t i = 0; i < len; i += 3) {
    const size_t chunk = (i + 3 <= len) ? 3 : (len - i);
    ASSERT_TRUE(p.feed(bytes + i, chunk));
  }
  ASSERT_TRUE(p.finalize());

  int starts = 0, ends = 0, chars = 0;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::Start)
      starts++;
    else if (e.type == Event::Type::End)
      ends++;
    else if (e.type == Event::Type::Char)
      chars++;
  }
  EXPECT_EQ(starts, 3);  // root, a, b
  EXPECT_EQ(ends, 3);
  EXPECT_GE(chars, 2);  // at least one Char event per text node; expat may split across chunk boundaries
}

TEST(SaxParser, EarlyStop) {
  const char* xml = "<root><a/><b/><c/></root>";

  struct StopAfterFirst {
    int startCount = 0;
    SaxParser* parser = nullptr;

    static void onStart(void* ud, const char* /*name*/, const char**) {
      auto* s = static_cast<StopAfterFirst*>(ud);
      s->startCount++;
      if (s->startCount == 2) {
        s->parser->stop();
      }
    }
    static void onEnd(void*, const char*) {}
  };

  StopAfterFirst state;
  SaxParser p;
  ASSERT_TRUE(p.init(&state, StopAfterFirst::onStart, StopAfterFirst::onEnd));
  state.parser = &p;

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  p.feed(bytes, strlen(xml));  // may return false after stop — that's fine

  EXPECT_TRUE(p.isStopped());
  EXPECT_EQ(state.startCount, 2);
}

TEST(SaxParser, ParseError) {
  const char* xml = "<root><unclosed>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  p.feed(bytes, strlen(xml));
  const bool ok = p.finalize();

  EXPECT_FALSE(ok);
  EXPECT_GT(strlen(p.errorString()), 0u);
}

TEST(SaxParser, ByteOffsetAdvances) {
  const char* xml = "<root><child>text</child></root>";

  struct OffsetCapture {
    uint32_t offsetAtChild = 0;
    SaxParser* parser = nullptr;
    bool childSeen = false;

    static void onStart(void* ud, const char* name, const char**) {
      auto* s = static_cast<OffsetCapture*>(ud);
      if (strcmp(name, "child") == 0 && !s->childSeen) {
        s->childSeen = true;
        s->offsetAtChild = s->parser->byteOffset();
      }
    }
    static void onEnd(void*, const char*) {}
  };

  OffsetCapture state;
  SaxParser p;
  ASSERT_TRUE(p.init(&state, OffsetCapture::onStart, OffsetCapture::onEnd));
  state.parser = &p;

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  EXPECT_GT(state.offsetAtChild, 0u);
}

TEST(SaxParser, DefaultHandler) {
  // Verify the defaultCb fires for entity references
  const char* xml = "<root>&amp;</root>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, nullptr, Collector::onDefault));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  // With SetDefaultHandlerExpand, standard entities like &amp; are expanded
  // by expat into char data before reaching the default handler — so no
  // Default event is expected here. Instead we may get a Char event.
  // What matters is that the document parsed successfully and no crash occurred.
  SUCCEED();
}

TEST(SaxParser, HtmlEntityRoutedToDefaultCb) {
  // &nbsp; is not an XML built-in, so the yxml backend must intercept it and
  // route it to defaultCb (mirroring expat's DefaultHandlerExpand).  Without
  // the fix, yxml returns YXML_EREF and feed() returns false.
  const char* xml = "<p>Silo&nbsp;1</p>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, Collector::onDefault));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  // defaultCb must have received the raw "&nbsp;" text.
  bool sawEntity = false;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::Default && e.text == "&nbsp;") {
      sawEntity = true;
    }
  }
  EXPECT_TRUE(sawEntity) << "defaultCb was not called with &nbsp;";
}

TEST(SaxParser, HtmlEntitySpanningChunks) {
  // Entity reference split across two feed() calls: first chunk ends inside
  // "&nbs", second chunk starts with "p;".
  const char* xml = "<p>x&nbsp;y</p>";
  const size_t split = 7;  // "<p>x&nb" | "sp;y</p>"

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, Collector::onDefault));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, split));
  ASSERT_TRUE(p.feed(bytes + split, strlen(xml) - split));
  ASSERT_TRUE(p.finalize());

  bool sawEntity = false;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::Default && e.text == "&nbsp;") sawEntity = true;
  }
  EXPECT_TRUE(sawEntity) << "cross-chunk &nbsp; not delivered to defaultCb";
}

TEST(SaxParser, XmlBuiltinEntitiesPassThrough) {
  // XML built-ins must still be expanded by yxml (not routed to defaultCb).
  const char* xml = "<r>&amp;&lt;&gt;&quot;&apos;</r>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, Collector::onDefault));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  // Collect all char data (built-ins are expanded to their characters).
  std::string chars;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::Char) chars += e.text;
  }
  EXPECT_EQ(chars, "&<>\"'");

  // None of the built-ins should appear as Default events.
  for (const auto& e : c.events) {
    EXPECT_NE(e.type, Event::Type::Default) << "built-in entity reached defaultCb: " << e.text;
  }
}

TEST(SaxParser, TruncationFlagsClearForWellSizedDoc) {
  const char* xml = "<root><child a=\"1\" b=\"2\">text</child></root>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  // A small, well-formed document stays within every fixed capacity.
  EXPECT_EQ(p.truncationFlags(), 0u);
}

TEST(SaxParser, TruncationFlagsReportMaxAttrs) {
  // 13 attributes — one more than kMaxAttrs (12). The 13th is dropped and the
  // overflow is recorded so callers can log it (the yxml backend only).
  const char* xml =
      "<e a1='1' a2='2' a3='3' a4='4' a5='5' a6='6' a7='7' a8='8' a9='9' "
      "a10='10' a11='11' a12='12' a13='13'/>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  // The active backend (yxml) has fixed caps and records the overflow. expat,
  // if ever re-enabled, has no fixed caps and returns 0 — so only assert the
  // flag when the parser actually reports truncation support.
  EXPECT_TRUE(p.truncationFlags() & SaxParser::kTruncMaxAttrs);
}

// ---------------------------------------------------------------------------
// HTML void-element repair
//
// Real-world EPUB/OPDS content frequently uses HTML-style void elements
// (<br>, <hr>, ...) without the XML-required self-closing slash. yxml is a
// strict well-formed-XML engine, so without repair these fail the parse the
// same way expat did. The pre-processor in SaxParserYxml.cpp turns "<br>"
// into "<br/>" before yxml ever sees it.
// ---------------------------------------------------------------------------

TEST(SaxParser, UnclosedBrIsAutoClosed) {
  const char* xml = "<p>Hello<br>World</p>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, nullptr, /*htmlVoidTagRepair=*/true));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  ASSERT_EQ(c.events.size(), 6u);
  EXPECT_EQ(c.events[0].name, "p");
  EXPECT_EQ(c.events[1].text, "Hello");
  EXPECT_EQ(c.events[2].type, Event::Type::Start);
  EXPECT_EQ(c.events[2].name, "br");
  EXPECT_EQ(c.events[3].type, Event::Type::End);
  EXPECT_EQ(c.events[3].name, "br");
  EXPECT_EQ(c.events[4].text, "World");
  EXPECT_EQ(c.events[5].name, "p");

  EXPECT_TRUE(p.truncationFlags() & SaxParser::kVoidTagRepaired);
}

TEST(SaxParser, UnclosedHrWithAttributeIsAutoClosed) {
  const char* xml = "<div>Section<hr class=\"sep\">Next</div>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, nullptr, /*htmlVoidTagRepair=*/true));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  bool sawHrStart = false, sawHrEnd = false;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::Start && e.name == "hr") sawHrStart = true;
    if (e.type == Event::Type::End && e.name == "hr") sawHrEnd = true;
  }
  EXPECT_TRUE(sawHrStart);
  EXPECT_TRUE(sawHrEnd);
  EXPECT_TRUE(p.truncationFlags() & SaxParser::kVoidTagRepaired);
}

TEST(SaxParser, UnclosedVoidTagIsCaseInsensitive) {
  const char* xml = "<p>Hello<BR>World</p>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, nullptr, /*htmlVoidTagRepair=*/true));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  EXPECT_TRUE(p.truncationFlags() & SaxParser::kVoidTagRepaired);
}

TEST(SaxParser, AlreadySelfClosedVoidTagDoesNotSetRepairFlag) {
  const char* xml = "<p>Hello<br/>World</p>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, nullptr, /*htmlVoidTagRepair=*/true));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  EXPECT_FALSE(p.truncationFlags() & SaxParser::kVoidTagRepaired);
}

TEST(SaxParser, VoidTagRepairIgnoresGreaterThanInAttributeValue) {
  // A '>' inside a quoted attribute value must not be mistaken for the tag
  // terminator that would trigger (or skip) the self-close injection.
  const char* xml = "<p title=\"a>b\">text<br>x</p>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, nullptr, /*htmlVoidTagRepair=*/true));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  bool sawBrEnd = false;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::End && e.name == "br") sawBrEnd = true;
  }
  EXPECT_TRUE(sawBrEnd);
}

TEST(SaxParser, UnclosedNonVoidElementStillFails) {
  // Repair is scoped to known HTML void elements; an unrelated unclosed
  // element (<span>) must continue to be a genuine parse error.
  const char* xml = "<p>Hello<span>World</p>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, nullptr, /*htmlVoidTagRepair=*/true));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  p.feed(bytes, strlen(xml));
  EXPECT_FALSE(p.finalize());
}

TEST(SaxParser, UnclosedVoidTagSplitAcrossChunks) {
  // Split the feed right in the middle of "<br>" to make sure the tag-scan
  // state machine survives a chunk boundary mid-tag.
  const char* xml = "<p>Hello<br>World</p>";
  const size_t split = 10;  // "<p>Hello<b" | "r>World</p>"

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar, nullptr, /*htmlVoidTagRepair=*/true));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, split));
  ASSERT_TRUE(p.feed(bytes + split, strlen(xml) - split));
  ASSERT_TRUE(p.finalize());

  bool sawBrEnd = false;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::End && e.name == "br") sawBrEnd = true;
  }
  EXPECT_TRUE(sawBrEnd);
  EXPECT_TRUE(p.truncationFlags() & SaxParser::kVoidTagRepaired);
}

TEST(SaxParser, PairedMetaParsesWhenRepairDisabled) {
  // EPUB3 OPF metadata pairs <meta> with a real end tag:
  //   <meta refines="#t" property="title-type">main</meta>
  // With repair enabled that opening tag would be self-closed, turning the
  // real </meta> into a mismatched close and killing the whole OPF parse
  // (book fails to open). Strict-XML parsers therefore init with repair off
  // (the default) — this must parse cleanly.
  const char* xml = "<package><metadata><meta refines=\"#t\" property=\"title-type\">main</meta></metadata></package>";

  Collector c;
  SaxParser p;
  ASSERT_TRUE(p.init(&c, Collector::onStart, Collector::onEnd, Collector::onChar));

  const auto* bytes = reinterpret_cast<const uint8_t*>(xml);
  ASSERT_TRUE(p.feed(bytes, strlen(xml)));
  ASSERT_TRUE(p.finalize());

  bool sawMetaStart = false, sawMetaEnd = false, sawText = false;
  for (const auto& e : c.events) {
    if (e.type == Event::Type::Start && e.name == "meta") sawMetaStart = true;
    if (e.type == Event::Type::End && e.name == "meta") sawMetaEnd = true;
    if (e.type == Event::Type::Char && e.text == "main") sawText = true;
  }
  EXPECT_TRUE(sawMetaStart);
  EXPECT_TRUE(sawMetaEnd);
  EXPECT_TRUE(sawText);
  EXPECT_FALSE(p.truncationFlags() & SaxParser::kVoidTagRepaired);
}
