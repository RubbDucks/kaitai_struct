package io.kaitai.struct

import org.scalatest.funspec.AnyFunSpec
import org.scalatest.matchers.should.Matchers

class JavaMainSpec extends AnyFunSpec with Matchers {
  describe("JavaMain.parseCommandLine") {
    it("keeps IR emission disabled by default") {
      val cfg = JavaMain.parseCommandLine(Array("-t", "python", "sample.ksy")).get
      cfg.emitIr shouldBe None
    }

    it("enables IR sidecar emission when --emit-ir is provided") {
      val cfg = JavaMain.parseCommandLine(Array("-t", "python", "--emit-ir", "out/sample.ksir", "sample.ksy")).get
      cfg.emitIr.map(_.toString) shouldBe Some("out/sample.ksir")
    }
  }
}
