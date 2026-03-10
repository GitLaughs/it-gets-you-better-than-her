#!/bin/bash
git checkout main
git checkout -b develop

branches=(
    "feature/yolov8-detection"
    "feature/depth-estimation"
    "feature/point-cloud"
    "feature/obstacle-avoidance"
    "feature/camera-driver"
    "feature/hand-interface"
    "feature/tracking"
    "feature/display-output"
    "feature/docker-setup"
)

for branch in "${branches[@]}"; do
    git checkout develop
    git checkout -b "$branch"
    echo "✅ Created: $branch"
done

git checkout develop
echo ""
echo "所有分支已创建，当前在 develop 分支"
git branch -a