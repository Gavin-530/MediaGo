<template>
  <el-container class="app-container">
    <el-aside width="200px" class="app-sidebar">
      <div class="logo">
        <h2>MediaGo</h2>
        <span class="subtitle">专业媒体处理</span>
      </div>
      <el-menu
        :default-active="activeMenu"
        router
        class="sidebar-menu"
      >
        <el-menu-item index="/">
          <el-icon><FolderOpened /></el-icon>
          <span>批量处理</span>
        </el-menu-item>
        <el-menu-item index="/probe">
          <el-icon><Search /></el-icon>
          <span>媒体探测</span>
        </el-menu-item>
      </el-menu>
    </el-aside>
    <el-main class="app-main">
      <router-view />
    </el-main>
  </el-container>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'

const route = useRoute()
const activeMenu = computed(() => route.path)
</script>

<style scoped>
.app-container {
  height: 100vh;
}

.app-sidebar {
  background: #1a1a2e;
  display: flex;
  flex-direction: column;
}

.logo {
  padding: 24px 20px 16px;
  text-align: center;
}

.logo h2 {
  color: #e0e0e0;
  font-size: 20px;
  margin: 0;
  font-weight: 600;
}

.logo .subtitle {
  color: #888;
  font-size: 12px;
}

.sidebar-menu {
  border-right: none;
  background: transparent;
}

.sidebar-menu .el-menu-item {
  color: #a0a0b0;
}

.sidebar-menu .el-menu-item:hover,
.sidebar-menu .el-menu-item.is-active {
  color: #fff;
  background: rgba(255, 255, 255, 0.08);
}

.app-main {
  background: #f5f7fa;
  padding: 24px;
  overflow-y: auto;
}
</style>
