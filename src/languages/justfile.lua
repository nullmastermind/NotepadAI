local L = {}

-- Justfile recipes are mostly shell, so the bash lexer gives the most
-- useful highlighting (strings, variables, keywords, comments) even
-- though it misses justfile-specific structure like recipe targets
-- (`name:`) and `set`/`import` directives.
L.lexer = "bash"

L.singleLineComment = "# "

L.tabSettings = "tabs"

L.first_line = {
	"^set%s+[%w%-]+%s*:=",
	"^import%s+[\"']",
	"^mod%s+[%w_]+",
}

-- '.justfile' -> suffix == "justfile"; 'foo.just' -> suffix == "just".
-- Bare 'justfile'/'Justfile'/'.justfile' files have no usable suffix and
-- are matched via L.filenames in detectLanguageFromExtension.
L.extensions = {
	"just",
	"justfile",
}

L.filenames = {
	"justfile",
	"Justfile",
	"JUSTFILE",
	".justfile",
}

L.keywords = {
	[0] = "alias as assert default else export fallback for function if import in mod private quiet set shell unexport",
}

L.styles = {
	["DEFAULT"] = {
		id = 0,
		fgColor = rgb(0x000000),
		bgColor = rgb(0xFFFFFF),
	},
	["ERROR"] = {
		id = 1,
		fgColor = rgb(0xFFFFFF),
		bgColor = rgb(0xFF0000),
	},
	["INSTRUCTION WORD"] = {
		id = 4,
		fgColor = rgb(0x0000FF),
		bgColor = rgb(0xFFFFFF),
		fontStyle = 1,
	},
	["NUMBER"] = {
		id = 3,
		fgColor = rgb(0xFF0000),
		bgColor = rgb(0xFFFFFF),
	},
	["STRING"] = {
		id = 5,
		fgColor = rgb(0x808080),
		bgColor = rgb(0xFFFFFF),
	},
	["CHARACTER"] = {
		id = 6,
		fgColor = rgb(0x808080),
		bgColor = rgb(0xFFFFFF),
	},
	["OPERATOR"] = {
		id = 7,
		fgColor = rgb(0x804000),
		bgColor = rgb(0xFFFFFF),
		fontStyle = 1,
	},
	["IDENTIFIER"] = {
		id = 8,
		fgColor = rgb(0x000000),
		bgColor = rgb(0xFFFFFF),
	},
	["SCALAR"] = {
		id = 9,
		fgColor = rgb(0xFF8040),
		bgColor = rgb(0xFFFFD9),
		fontStyle = 1,
	},
	["COMMENT LINE"] = {
		id = 2,
		fgColor = rgb(0x008000),
		bgColor = rgb(0xFFFFFF),
	},
	["PARAM"] = {
		id = 10,
		fgColor = rgb(0x008080),
		bgColor = rgb(0x00FFFF),
	},
	["BACKTICKS"] = {
		id = 11,
		fgColor = rgb(0x804040),
		bgColor = rgb(0xE1FFF3),
		fontStyle = 1,
	},
	["HERE DELIM"] = {
		id = 12,
		fgColor = rgb(0xFFFF00),
		bgColor = rgb(0xFF0000),
		fontStyle = 1,
	},
	["HERE Q"] = {
		id = 13,
		fgColor = rgb(0xFF0000),
		bgColor = rgb(0xFFFF80),
	},
}
return L
