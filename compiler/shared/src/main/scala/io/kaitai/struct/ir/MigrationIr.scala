package io.kaitai.struct.ir

import io.kaitai.struct.datatype.DataType
import io.kaitai.struct.datatype.DataType._
import io.kaitai.struct.datatype.{BigEndian, FixedEndian, LittleEndian}
import io.kaitai.struct.exprlang.Ast
import io.kaitai.struct.format._

object MigrationIr {
  def serialize(spec: ClassSpec): String = {
    val out = new StringBuilder
    out.append("KSIR1\n")
    out.append(s"name ${quote(spec.nameAsStr)}\n")
    out.append(s"default_endian ${endianToIr(spec.meta.endian.collect { case f: FixedEndian => f })}\n")

    val types = spec.types.values.toList.map(t => TypeRow(t.name.lastOption.getOrElse(t.nameAsStr), TypeRef.user(t.nameAsStr)))
    out.append(s"types ${types.size}\n")
    types.foreach { t =>
      out.append(s"type ${quote(t.name)} ${renderTypeRef(t.typeRef)}\n")
    }

    out.append(s"attrs ${spec.seq.size}\n")
    spec.seq.foreach { attr =>
      val (typ, endianOverride, sizeExpr, enumName, encoding, ifExpr, repeatKind, repeatExpr, switchOn, switchCases) = attrToIr(attr)
      val endStr = endianOverride.map(e => endianToIr(Some(e))).getOrElse("none")
      val sizeStr = sizeExpr.map(exprToIr).getOrElse("none")
      val ifStr = ifExpr.map(exprToIr).getOrElse("none")
      val repeatExprStr = repeatExpr.map(exprToIr).getOrElse("none")
      val switchOnStr = switchOn.map(exprToIr).getOrElse("none")
      val switchCasesStr = switchCases.map { case (onExpr, tpe) =>
        s" ${quote(onExpr.map(exprToIr).getOrElse("else"))} ${renderTypeRef(tpe)}"
      }.mkString("")
      out.append(s"attr ${quote(attr.id.humanReadable)} ${renderTypeRef(typ)} $endStr ${quote(sizeStr)} ${quote(enumName.getOrElse("none"))} ${quote(encoding.getOrElse("none"))} ${quote(ifStr)} $repeatKind ${quote(repeatExprStr)} ${quote(switchOnStr)} ${switchCases.size}$switchCasesStr\n")
    }

    val enums = collectEnums(spec)
    out.append(s"enums ${enums.size}\n")
    enums.foreach { enumRow =>
      out.append(s"enum ${quote(enumRow.name)} ${enumRow.values.size}\n")
      enumRow.values.foreach { value =>
        out.append(s"enum_value ${value.value} ${quote(value.name)}\n")
      }
    }

    val valueInstances = spec.instances.values.collect { case v: ValueInstanceSpec => v }.toList
    out.append(s"instances ${valueInstances.size}\n")
    valueInstances.foreach { inst =>
      out.append(s"instance ${quote(inst.id.humanReadable)} ${quote(exprToIr(inst.value))}\n")
    }

    val validations = collectValidations(spec)
    out.append(s"validations ${validations.size}\n")
    validations.foreach { v =>
      out.append(s"validation ${quote(v.target)} ${quote(exprToIr(v.conditionExpr))} ${quote(v.message)}\n")
    }

    out.append("end\n")
    out.toString
  }

  private case class TypeRow(name: String, typeRef: TypeRef)
  private case class EnumValueRow(value: Long, name: String)
  private case class EnumRow(name: String, values: Seq[EnumValueRow])
  private case class ValidationRow(target: String, conditionExpr: Ast.expr, message: String)

  private case class TypeRef(kind: String, payload: String)
  private object TypeRef {
    def primitive(name: String): TypeRef = TypeRef("primitive", name)
    def user(name: String): TypeRef = TypeRef("user", name)
  }

  private def renderTypeRef(t: TypeRef): String = s"${t.kind} ${quote(t.payload)}"

  private def quote(s: String): String = {
    val escaped = s
      .replace("\\", "\\\\")
      .replace("\"", "\\\"")
      .replace("\n", "\\n")
      .replace("\r", "\\r")
      .replace("\t", "\\t")
    s"\"$escaped\""
  }

  private def endianToIr(endian: Option[FixedEndian]): String = endian match {
    case Some(LittleEndian) => "le"
    case Some(BigEndian) => "be"
    case None => "le"
  }

  private def attrToIr(attr: AttrSpec): (TypeRef, Option[FixedEndian], Option[Ast.expr], Option[String], Option[String], Option[Ast.expr], String, Option[Ast.expr], Option[Ast.expr], Seq[(Option[Ast.expr], TypeRef)]) = {
    val t = attr.dataType
    val endianOverride = t match {
      case IntMultiType(_, _, e) => e
      case FloatMultiType(_, e) => e
      case _ => None
    }
    val sizeExpr = t match {
      case BytesLimitType(size, _, _, _, _) => Some(size)
      case StrFromBytesType(BytesLimitType(size, _, _, _, _), _) => Some(size)
      case StrFromBytesTypeUnknownEncoding(BytesLimitType(size, _, _, _, _)) => Some(size)
      case StrzType(BytesLimitType(size, _, _, _, _), _) => Some(size)
      case _ => None
    }
    val enumName = t match {
      case EnumType(name, _) => Some(name.mkString("::"))
      case _ => None
    }
    val encoding = t match {
      case StrFromBytesType(_, enc) => Some(enc)
      case StrzType(_, Some(enc)) => Some(enc)
      case _ => None
    }
    val (switchOn, switchCases) = t match {
      case sw: SwitchType =>
        val cases = sw.cases.toSeq.map { case (k, v) =>
          val keyExpr = if (k == SwitchType.ELSE_CONST) None else Some(k)
          (keyExpr, typeRefFromDataType(v))
        }
        (Some(sw.on), cases)
      case _ => (None, Seq.empty)
    }
    val (repeatKind, repeatExpr) = attr.cond.repeat match {
      case RepeatExpr(expr) => ("expr", Some(expr))
      case RepeatUntil(expr) => ("until", Some(expr))
      case RepeatEos => ("eos", None)
      case NoRepeat => ("none", None)
    }
    (typeRefFromDataType(t), endianOverride, sizeExpr, enumName, encoding, attr.cond.ifExpr, repeatKind, repeatExpr, switchOn, switchCases)
  }

  private def collectEnums(spec: ClassSpec): Seq[EnumRow] = {
    spec.enums.values.toSeq.map { enumSpec =>
      EnumRow(
        enumSpec.nameAsStr,
        enumSpec.map.toSeq.map { case (id, valueSpec) => EnumValueRow(id, valueSpec.name) }
      )
    }
  }

  private def typeRefFromDataType(t: DataType): TypeRef = t match {
    case Int1Type(signed) => TypeRef.primitive(if (signed) "s1" else "u1")
    case IntMultiType(signed, width, _) =>
      TypeRef.primitive((if (signed) "s" else "u") + width.width)
    case FloatMultiType(width, _) => TypeRef.primitive("f" + width.width)
    case _: StrType => TypeRef.primitive("str")
    case _: BytesType => TypeRef.primitive("bytes")
    case ut: UserType => TypeRef.user(ut.name.mkString("::"))
    case EnumType(_, basedOn) => typeRefFromDataType(basedOn)
    case _ => TypeRef.primitive("bytes")
  }

  private def collectValidations(spec: ClassSpec): List[ValidationRow] = {
    val attrValidations = spec.seq.flatMap(attr => attr.valid.map(v => validationToRow(attr.id.humanReadable, v)))
    val parseInstValidations = spec.instances.values.toList.collect {
      case p: ParseInstanceSpec if p.valid.nonEmpty => validationToRow(p.id.humanReadable, p.valid.get)
    }
    attrValidations ++ parseInstValidations
  }

  private def validationToRow(target: String, v: ValidationSpec): ValidationRow = {
    val targetExpr = Ast.expr.Name(Ast.identifier(target))
    val cond = v match {
      case ValidationEq(value) => Ast.expr.Compare(targetExpr, Ast.cmpop.Eq, value)
      case ValidationMin(min) => Ast.expr.Compare(targetExpr, Ast.cmpop.GtE, min)
      case ValidationMax(max) => Ast.expr.Compare(targetExpr, Ast.cmpop.LtE, max)
      case ValidationRange(min, max) =>
        Ast.expr.BoolOp(Ast.boolop.And, Seq(
          Ast.expr.Compare(targetExpr, Ast.cmpop.GtE, min),
          Ast.expr.Compare(targetExpr, Ast.cmpop.LtE, max)
        ))
      case ValidationAnyOf(values) =>
        Ast.expr.BoolOp(Ast.boolop.Or, values.map(value => Ast.expr.Compare(targetExpr, Ast.cmpop.Eq, value)))
      case ValidationExpr(checkExpr) => checkExpr
      case ValidationInEnum() => Ast.expr.Name(Ast.identifier("__in_enum_validation__"))
    }
    ValidationRow(target, cond, "")
  }

  private def exprToIr(expr: Ast.expr): String = expr match {
    case Ast.expr.IntNum(n) => s"(int $n)"
    case Ast.expr.Bool(n) => s"(bool $n)"
    case Ast.expr.Name(id) => s"(name ${quote(id.name)})"
    case Ast.expr.InternalName(id) => s"(name ${quote(id.humanReadable)})"
    case Ast.expr.UnaryOp(op, operand) => s"(un ${quote(unaryOp(op))} ${exprToIr(operand)})"
    case Ast.expr.BinOp(left, op, right) => s"(bin ${quote(binaryOp(op))} ${exprToIr(left)} ${exprToIr(right)})"
    case Ast.expr.Compare(left, op, right) => s"(bin ${quote(compareOp(op))} ${exprToIr(left)} ${exprToIr(right)})"
    case Ast.expr.BoolOp(op, values) =>
      values.reduceOption((l, r) => Ast.expr.BinOp(l, if (op == Ast.boolop.And) Ast.operator.BitAnd else Ast.operator.BitOr, r)) match {
        case Some(chained) => exprToIr(chained)
        case None => "(bool true)"
      }
    case unsupported => s"(name ${quote(s"__unsupported_expr__:${unsupported.toString}")})"
  }

  private def unaryOp(op: Ast.unaryop): String = op match {
    case Ast.unaryop.Not => "!"
    case Ast.unaryop.Minus => "-"
    case Ast.unaryop.Invert => "~"
  }

  private def binaryOp(op: Ast.operator): String = op match {
    case Ast.operator.Add => "+"
    case Ast.operator.Sub => "-"
    case Ast.operator.Mult => "*"
    case Ast.operator.Div => "/"
    case Ast.operator.Mod => "%"
    case Ast.operator.LShift => "<<"
    case Ast.operator.RShift => ">>"
    case Ast.operator.BitOr => "|"
    case Ast.operator.BitXor => "^"
    case Ast.operator.BitAnd => "&"
  }

  private def compareOp(op: Ast.cmpop): String = op match {
    case Ast.cmpop.Eq => "=="
    case Ast.cmpop.NotEq => "!="
    case Ast.cmpop.Lt => "<"
    case Ast.cmpop.LtE => "<="
    case Ast.cmpop.Gt => ">"
    case Ast.cmpop.GtE => ">="
  }
}
