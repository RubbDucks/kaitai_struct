package io.kaitai.struct.languages.components

import org.scalatest.funspec.AnyFunSpec
import org.scalatest.matchers.should.Matchers

class LanguageCompilerStaticSpec extends AnyFunSpec with Matchers {
  describe("LanguageCompilerStatic") {
    it("registers the Wireshark Lua compiler target") {
      LanguageCompilerStatic.NAME_TO_CLASS.keySet should contain("wireshark_lua")
    }
  }
}
