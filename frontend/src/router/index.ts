import { createRouter, createWebHashHistory } from 'vue-router'
import BatchPage from '../views/BatchPage.vue'
import ProbePage from '../views/ProbePage.vue'

const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', name: 'batch', component: BatchPage },
    { path: '/probe', name: 'probe', component: ProbePage },
  ],
})

export default router
