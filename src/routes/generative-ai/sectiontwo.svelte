<script>
	import { quartInOut } from 'svelte/easing';
	import { fade } from 'svelte/transition';
	import { onMount } from 'svelte';

	let words = ['Mobile', 'Desktop', 'Browser'];
	let images = ['src/images/image.png', 'src/images/image (1).png', 'src/images/image (2).png'];

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

	<div class="bg-cover bg-gray-300 rounded-3xl card text-primary-content shadow-xl">
		<div class="card-body p-10">
			<div
				class="font-bold hover:bg-primary p-8 hover:border-black mx-auto rounded-3xl text-center text-2xl lg:w-2/3 transition duration-500 ease-in-out"
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

		<div class=" justify-items-center items-center rounded-3xl p-10">
			{#key currentImage}
				<div
					class=" font-bold hover:bg-primary p-8 hover:border-black mx-auto rounded-3xl text-center text-2xl lg:w-2/3 transition duration-500 ease-in-out"
				>
					{#if show}
						<span transition:fade={{ duration: 1000 }}>
							<img class=" mx-auto w-50 h-80" src={currentImage} alt="device image" />
						</span>
					{/if}

					<p class="text-center mt-4 text-xl">{currentWord}</p>
				</div>
			{/key}
		</div>

		<p class="mt-4 text-lg p-4">
			Want to try running <a
				href="https://onnxruntime.ai/docs/tutorials/"
				class="underline hover:text-blue-500 font-bold transition duration-500 ease-in-out">these</a
			> yourself?
		</p>
		<p class="mt-2 text-lg font-bold p-4">Here's How!</p>
		<ul class="steps steps-vertical overflow-auto max-h-64 p-4">
			<li class="step step-primary">Choose your language: Python, C#, C++, or JavaScript</li>
			<li class="step step-primary">Install for CPU</li>
			<li class="step">Install for GPU (if needed)</li>
			<li class="step">Install Special Environments: Web, Node.js, React Native</li>
			<li class="step">Click the Install button below for more detailed instructions.</li>
		</ul>
		<a href="https://onnxruntime.ai/docs/install/">
			<div class="card-actions justify-end p-8">
				<button class="bg-primary btn btn-primary">Install</button>
			</div>
		</a>
	</div>
</div>
