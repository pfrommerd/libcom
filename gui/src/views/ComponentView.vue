<template>
	<Bubble hasContent>
		<template v-slot:header>
			<div class="items header-items">
				<span> {{ component.name }} </span>
				<span class="destroy icon" @click.stop="destroy">
					<font-awesome-icon icon="trash" />
				</span>
			</div>
		</template>
		<template v-slot:content>
			<div class="items content-items">
				<span class="content-type">
					{{ component.type }}
				</span>
				<span class="content-icon icon" @click.stop="streamRequest">
					<font-awesome-icon icon="exchange-alt" />
				</span>
				<span class="content-icon icon" @click.stop="info">
					<font-awesome-icon icon="question" />
				</span>
			</div>
		</template>
	</Bubble>
</template>

<script>
import { Context } from "telegraph";

import Bubble from "../components/Bubble.vue";

// globally register the componentinfopage
import ComponentInfoPage from "../pages/ComponentInfoPage.vue";
import StreamRequestPage from "../pages/StreamRequestPage.vue";

import uuidv4 from "uuid/v4";
import Vue from "vue";

Vue.component("ComponentInfoPage", ComponentInfoPage);
Vue.component("StreamRequestPage", StreamRequestPage);

export default {
	name: "ComponentView",
	components: { Bubble },
	props: {
		component: Context,
	},
	methods: {
		destroy() {
			if (this.component) {
				this.component.destroy();
			}
		},
		streamRequest() {
			var popup = {
				name: "Requests for " + this.component.name,
				type: "StreamRequestPage",
				props: { resource: this.component },
				id: uuidv4(),
			};
			this.$bubble("popup", popup);
		},
		info() {
			// create the popup
			var popup = {
				name: "Component Information",
				type: "ComponentInfoPage",
				props: { component: this.component },
				id: uuidv4(),
				root: null,
			};
			this.$bubble("popup", popup);
		},
	},
};
</script>
<style scoped>
.items {
	display: flex;
	flex-direction: row;
	padding-right: 12px;
	flex: 1;
}
.content-items {
	justify-content: flex-start;
	padding-right: 10px;
	padding-left: 10px;
	padding-top: 3px;
	padding-bottom: 4px;
}
.header-items {
	justify-content: space-between;
}
.icon {
	color: var(--tab-color);
	transition: color 0.3s;
}
.content-icon {
	font-size: 0.8rem;
	margin: 2px;
}
.content-icon:hover {
	color: var(--content-hover);
}
.destroy {
	font-size: 0.7rem;
	transition: color 0.3s;
	align-self: center;
}
.destroy:hover {
	color: #ed4949;
}

.content-type {
	color: var(--accent-color);
	width: 100%;
}
</style>
