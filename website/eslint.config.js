import eslintPluginAstro from "eslint-plugin-astro"
import js from "@eslint/js"
import globals from "globals"
import ts from "typescript-eslint"

export default [
  js.configs.recommended,
  ...ts.configs.strict,
  ...ts.configs.stylistic,
  ...eslintPluginAstro.configs.recommended,
  ...eslintPluginAstro.configs["jsx-a11y-recommended"],
  {
    linterOptions: {
      reportUnusedInlineConfigs: "error",
    },
  },
  {
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
      globals: {
        ...globals.browser,
        ...globals.node,
      },
    },
  },
  {
    files: ["*.astro"],
    parser: "astro-eslint-parser",
  },
  {
    ignores: ["dist/*", ".astro/*"],
  },
  {
    rules: {
      "@typescript-eslint/no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
        },
      ],
      "@typescript-eslint/consistent-type-definitions": ["error", "type"],
    },
  },
]
