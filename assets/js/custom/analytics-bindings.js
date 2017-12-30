$(document).ready(function() {
	let elementClassToAction = {
		".series-post-entry-link": "Series entry",
		".previous-post-top":      "Previous post top",
		".next-post-top":          "Next post top",
		".previous-post":          "Previous post bottom",
		".next-post":              "Next post bottom",
		".related-post":           "Related post",
	};

	for(elementClass in elementClassToAction) {
		(function (elementClass, action) {
			$(elementClass).on("click", (event) => {
				ga("send", "event", "Link", action, $(event.target).attr("href"));
			});
		})(elementClass, elementClassToAction[elementClass]);
	}
});