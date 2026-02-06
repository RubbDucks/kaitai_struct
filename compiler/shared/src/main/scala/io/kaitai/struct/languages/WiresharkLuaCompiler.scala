package io.kaitai.struct.languages

import io.kaitai.struct.{ClassTypeProvider, RuntimeConfig}
import io.kaitai.struct.languages.components.{ExceptionNames, LanguageCompiler, LanguageCompilerStatic, StreamStructNames, UpperCamelCaseClasses}

class WiresharkLuaCompiler(typeProvider: ClassTypeProvider, config: RuntimeConfig)
  extends LuaCompiler(typeProvider, config) {

  override def outFileName(topClassName: String): String = s"${topClassName}_wireshark.lua"

  override def fileFooter(topClassName: String): Unit = {
    val protoVar = s"${topClassName}_proto"
    val protoDisplay = type2class(topClassName)

    out.puts
    out.puts("-- Wireshark Lua dissector")
    out.puts(s"local $protoVar = Proto(\"$topClassName\", \"$protoDisplay\")")
    out.puts
    out.puts(s"function $protoVar.dissector(tvb, pinfo, tree)")
    out.inc
    out.puts(s"pinfo.cols.protocol = \"$protoDisplay\"")
    out.puts(s"local subtree = tree:add($protoVar, tvb())")
    out.puts("local status, parsed = pcall(function()")
    out.inc
    out.puts(s"return ${type2class(topClassName)}:from_string(tvb:range():string())")
    out.dec
    out.puts("end)")
    out.puts("if not status then")
    out.inc
    out.puts("subtree:add_expert_info(PI_MALFORMED, PI_ERROR, \"Kaitai Struct parse error: \" .. parsed)")
    out.dec
    out.puts("end")
    out.dec
    out.puts("end")
    out.puts
    out.puts("-- Register the dissector on the desired port by setting this.")
    out.puts(s"local ${protoVar}_default_port = 0")
    out.puts(s"if ${protoVar}_default_port > 0 then")
    out.inc
    out.puts(s"DissectorTable.get(\"tcp.port\"):add(${protoVar}_default_port, $protoVar)")
    out.dec
    out.puts("end")
  }
}

object WiresharkLuaCompiler extends LanguageCompilerStatic
  with UpperCamelCaseClasses
  with StreamStructNames
  with ExceptionNames {
  override def getCompiler(
    tp: ClassTypeProvider,
    config: RuntimeConfig
  ): LanguageCompiler = new WiresharkLuaCompiler(tp, config)

  override def kstructName: String = LuaCompiler.kstructName
  override def kstreamName: String = LuaCompiler.kstreamName
  override def ksErrorName(err: io.kaitai.struct.datatype.KSError): String =
    LuaCompiler.ksErrorName(err)
}
