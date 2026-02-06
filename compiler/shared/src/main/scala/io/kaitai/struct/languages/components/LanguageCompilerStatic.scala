package io.kaitai.struct.languages.components

import io.kaitai.struct._
import io.kaitai.struct.languages._

trait LanguageCompilerStatic {
  def getCompiler(tp: ClassTypeProvider, config: RuntimeConfig): LanguageCompiler
}

object LanguageCompilerStatic {
  val NAME_TO_CLASS: Map[String, LanguageCompilerStatic] = Map(
    "cpp_stl" -> CppCompiler,
    "lua" -> LuaCompiler,
    "wireshark_lua" -> WiresharkLuaCompiler,
    "python" -> PythonCompiler,
    "ruby" -> RubyCompiler
  )

  val CLASS_TO_NAME: Map[LanguageCompilerStatic, String] = NAME_TO_CLASS.map(_.swap)

  def byString(langName: String): LanguageCompilerStatic = NAME_TO_CLASS(langName)
}
