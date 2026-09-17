// Clay ships no multi-line text component, and a route is inherently
// multi-line, so we register one. This function is serialized with
// toString() and injected into the config page, so it can close over
// nothing: no require, no outer variables, templates inline as strings.

module.exports = function () {
  this.registerComponent({
    name: "textarea",
    manipulator: "val",
    defaults: {
      label: "",
      description: "",
      attributes: {}
    },
    style: [
      ".component-textarea label { display: block; }",
      ".component-textarea .label { display: block; padding-bottom: 0.35rem; }",
      ".component-textarea textarea {",
      "  display: block;",
      "  width: 100%;",
      "  margin-top: 0.7rem;",
      "  padding: 0.35rem 0.375rem;",
      "  background: #333333;",
      "  color: #ffffff;",
      "  border: none;",
      "  border-radius: 0.25rem;",
      "  font-family: inherit;",
      "  font-size: inherit;",
      "  line-height: 1.4;",
      "  resize: vertical;",
      "  -webkit-appearance: none;",
      "  appearance: none;",
      "}",
      ".component-textarea textarea::placeholder { color: #858585; }",
      ".component-textarea textarea:focus { outline: none; box-shadow: none; }",
      ".section .component-textarea { padding: 0; }"
    ].join("\n"),
    template: [
      '<div class="component component-textarea">',
      '  <label class="tap-highlight">',
      '    <span class="label">{{{label}}}</span>',
      "    <textarea data-manipulator-target",
      '      {{each key: attributes}}{{key}}="{{this}}"{{/each}}></textarea>',
      "  </label>",
      "  {{if description}}",
      '    <div class="description">{{{description}}}</div>',
      "  {{/if}}",
      "</div>"
    ].join("\n")
  });
};
