$(document).ready(function() {
	// might not have it, it's not in all layouts
	let $progressBar = $("#scroll-progress-bar");
	if($progressBar === undefined) {
		return;
	}

	let $window = $(window);
	let $document = $(document);
	let barColor = "rgba(62, 130, 150, 255)";
	let transparent = "rgba(0, 0, 0, 0)";

	let setCurrentProgress = function($progressBar, $window, $document) {
		let baseX = $window.height() / $document.height() * 100.0;
		let x = $window.scrollTop() / $document.height() * 100.0 + baseX;
		let commonGradient = "(left, " + barColor + " " + x + "%, " + transparent + " " + x + "%)";

		let vendorGradient = "";
		vendorGradient += "background: -webkit-linear-gradient" + commonGradient + ";";
		vendorGradient += "background: -moz-linear-gradient" + commonGradient + ";";
		vendorGradient += "background: -ms-linear-gradient" + commonGradient + ";";
		vendorGradient += "background: -o-linear-gradient" + commonGradient + ";";
		vendorGradient += "background: linear-gradient" + commonGradient + ";";

		$progressBar.attr("style", vendorGradient);
	};

	setCurrentProgress($progressBar, $window, $document);
	$window.scroll(function(e) {
		setCurrentProgress($progressBar, $window, $document);
	});
});