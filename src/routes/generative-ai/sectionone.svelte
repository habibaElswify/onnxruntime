<script>
	import { quartInOut } from 'svelte/easing';
	import { fade } from 'svelte/transition';
	import { onMount } from 'svelte';
	import mobileExample from '../../images/mobile_example_1024_rect_cropped.png';
	import desktopExample from '../../images/desktop_example_1024_rect_cropped.png';
	import browserExample from '../../images/browser_example_1024_rect_cropped.png';

	let words = ['Mobile', 'Desktop', 'Browser'];
	let images = [mobileExample, desktopExample, browserExample];

	let currentWordIndex = 0;
	let currentWord = words[currentWordIndex];
	let currentImage = images[currentWordIndex];
	let show = true;

	onMount(() => {
		const interval = setInterval(() => {
			show = false;
			setTimeout(() => {
				currentWordIndex = (currentWordIndex + 1) % words.length;
				currentWord = words[currentWordIndex];
				currentImage = images[currentWordIndex];
				show = true;
			}, 1000);
		}, 3000);

		return () => clearInterval(interval);
	});
</script>

<div class="container mx-auto px-4 py-8">
	<h2 class="mb-8 text-4xl font-bold text-center card-title">Use ONNXRuntime - GenAI</h2>
	<div
		class="grid grid-cols-1 justify-items-center items-center p-4 lg:grid-cols-3 rounded-3xl bg-gray-300"
	>
		<div class="rounded-3xl card text-primary-content lg:col-span-2">
			<div class="card-body p-4">
				<div
					class="font-bold p-2 hover:border-black mx-auto rounded-3xl text-center text-2xl lg:w-2/3 transition duration-500 ease-in-out"
				>
					Run ONNX - GenAI on
					<br />
					{#if show}
						<span transition:fade={{ duration: 1000 }}>
							{currentWord}
						</span>
					{/if}
				</div>
			</div>
			{#key currentImage}
				<div class="mx-auto py-4">
					{#if show}
						<span transition:fade={{ duration: 1000 }}>
							<img class="w-1/2 mx-auto" src={currentImage} alt="device" />
						</span>
					{/if}
				</div>
			{/key}
		</div>
		<div class="mx-auto">
			<p class="text-lg">Want to try running these yourself?</p>
			<p class="mt-2 text-lg font-bold">Here's How!</p>
			<ul class="steps steps-vertical overflow-auto">
				<li class="step step-primary">
					<a
						href="https://github.com/microsoft/onnxruntime-inference-examples/tree/main/mobile/examples/phi-3/android"
					>
						<div class="card-actions justify-end p-8">
							<button class="bg-primary btn btn-primary"
								><p class="text-2xl">Run on Mobile</p></button
							>
						</div>
					</a>
				</li>
				<li class="step step-primary">
					<a href="https://onnxruntime.ai/docs/genai/tutorials/phi3-v.html">
						<div class="card-actions justify-end p-8">
							<button class="bg-primary btn btn-primary"
								><p class="text-2xl">Run on Desktop</p></button
							>
						</div>
					</a>
				</li>
				<li class="step step-primary">
					<a href="https://github.com/microsoft/onnxruntime-inference-examples/tree/main/js/chat">
						<div class="card-actions justify-end p-8">
							<button class="bg-primary btn btn-primary"
								><p class="text-2xl">Run in Browser</p></button
							>
						</div>
					</a>
				</li>
			</ul>
		</div>
	</div>
</div>
