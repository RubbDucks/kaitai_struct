package io.kaitai.struct.translators

import io.kaitai.struct.datatype.DataType._
import io.kaitai.struct.exprlang.Expressions
import io.kaitai.struct.precompile.TypeMismatchError
import org.scalatest.funspec.AnyFunSpec
import org.scalatest.matchers.should.Matchers._

class TypeDetector$Test extends AnyFunSpec {
  describe("TypeDetector") {
    it("combines ints properly") {
      val ut1 = CalcUserType(List("foo"), None)
      val ut2 = CalcUserType(List("bar"), None)

      TypeDetector.combineTypes(ut1, ut2) should be(CalcKaitaiStructType(false))
    }

    it("rejects direct calls on non-method expressions") {
      val detector = new TypeDetector(TestTypeProviders.Always(CalcIntType))
      val ex = Expressions.parse("(1 + 2)(3)")

      val thrown = the [TypeMismatchError] thrownBy detector.detectType(ex)
      thrown.getMessage should be("can't call expression BinOp(IntNum(1),Add,IntNum(2)) directly; only method calls like `obj.method(...)` are supported")
    }

  }
}
