var preCompilationJS = require("./preCompilationJS");
var preCompilationC = require("./preCompilationC");
var jamCTranslator = require("./jamCTranslator");
var jamJSTranslator = require("./jamJSTranslator");
var callGraph = require("./callGraph");
const VarTableFile = require("./VarTable");

function compile(cInput, jsInput, lineNumber, yieldPoint) {
  let cTableManager = new VarTableFile.TableManager(
    new VarTableFile.VarTable(null)
  );
  let jsTableManager = new VarTableFile.TableManager(
    new VarTableFile.VarTable(null)
  );
  cTableManager.setCheckSideEffect(true);
  jsTableManager.setCheckSideEffect(true);
  console.log("Side effect checking enabled");

  var libTable = [];

  // Pre-compilation State.
  preCompilationJS.compile(jsInput, jsTableManager);
  preCompilationC.compile(cInput);

  // Main Compilation.
  var jsResults = jamJSTranslator.compile(
    jsInput,
    jsTableManager,
    yieldPoint,
    libTable
  );
  var cResults = jamCTranslator.compile(
    cInput,
    lineNumber,
    cTableManager,
    yieldPoint,
    libTable,
    jsResults.jconds
  );

  callGraph.pruneJSCallGraph();
  callGraph.checkCalls();

  var preamble = "\njsys = jworklib.getjsys();\n";
  if (jsResults.hasJdata)
    preamble += "jman = new JAMManager(jworklib.getcmdopts(), jsys);\n";

  return {
    C: cResults.C,
    JS: jsResults.JS.jsout,
    jstart: jsResults.JS.jstart,
    annotated_JS: jsResults.JS.jsout + cResults.JS,
    maxLevel: jsResults.maxLevel,
    hasJdata: jsResults.hasJdata,
    C_SideEffectTable: cTableManager.getSideEffectResult(),
    JS_SideEffectTable: jsTableManager.getSideEffectResult(),
  };
}

module.exports = {
  compile: compile,
};
