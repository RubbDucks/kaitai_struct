package io.kaitai.struct.ir

import io.kaitai.struct.format.ClassSpec
import io.kaitai.struct.formats.JavaKSYParser
import org.scalatest.funspec.AnyFunSpec
import org.scalatest.matchers.should.Matchers

import scala.io.Source

class MigrationIrSpec extends AnyFunSpec with Matchers {
  describe("MigrationIr.serialize") {
    it("exports deterministic IR for representative attrs and instances") {
      val spec = parseSpec(
        """meta:
          |  id: ir_sample
          |  endian: le
          |seq:
          |  - id: len
          |    type: u2
          |  - id: data
          |    size: len + 1
          |instances:
          |  tail_len:
          |    value: len - 1
          |""".stripMargin,
        "ir_sample.ksy"
      )

      MigrationIr.serialize(spec) shouldEqual loadFixture("ir_sample.ksir")
    }

    it("exports deterministic IR for validations") {
      val spec = parseSpec(
        """meta:
          |  id: ir_validation
          |  endian: be
          |seq:
          |  - id: a
          |    type: u1
          |    valid:
          |      min: 3
          |      max: 9
          |  - id: b
          |    type: u1
          |    valid:
          |      any-of: [1, 2]
          |instances:
          |  is_big:
          |    value: a >= 4
          |""".stripMargin,
        "ir_validation.ksy"
      )

      MigrationIr.serialize(spec) shouldEqual loadFixture("ir_validation.ksir")
    }
  }

  private def parseSpec(yaml: String, fileName: String): ClassSpec = {
    ClassSpec.fromYaml(JavaKSYParser.stringToYaml(yaml), Some(fileName))
  }

  private def loadFixture(name: String): String = {
    val stream = getClass.getClassLoader.getResourceAsStream(s"io/kaitai/struct/ir/$name")
    val source = Source.fromInputStream(stream, "UTF-8")
    try source.mkString finally source.close()
  }
}
