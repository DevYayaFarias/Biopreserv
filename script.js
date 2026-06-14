document.addEventListener('DOMContentLoaded', () => {
    // Modal Logic
    const modal = document.getElementById('sobre-modal');
    const btnOpen = document.getElementById('open-modal');
    
    if(btnOpen && modal) {
        btnOpen.addEventListener('click', () => {
            modal.classList.add('active');
        });

        modal.addEventListener('click', (e) => {
            if (e.target === modal) {
                modal.classList.remove('active');
            }
        });
    }

    // Toggles (Simple View)
    const toggleButtons = document.querySelectorAll('.btn-toggle');
    toggleButtons.forEach(button => {
        button.addEventListener('click', function() {
            const isActive = this.classList.contains('active');
            const deviceName = this.getAttribute('data-device');
            
            if (isActive) {
                this.classList.remove('active');
                this.textContent = `${deviceName} OFF`;
            } else {
                this.classList.add('active');
                this.textContent = `${deviceName}: ON`;
            }
        });
    });

    // Save Config Button
    const saveButton = document.querySelector('.btn-save');
    if(saveButton) {
        saveButton.addEventListener('click', () => {
            const originalText = saveButton.textContent;
            saveButton.textContent = "SALVO!";
            saveButton.style.backgroundColor = "#00ff88";
            saveButton.style.color = "#000";
            
            setTimeout(() => {
                saveButton.textContent = originalText;
                saveButton.style.backgroundColor = "";
                saveButton.style.color = "";
            }, 2000);
        });
    }

    // View Toggle Logic (Simple <-> Advanced)
    const toggleViewBtn = document.getElementById('toggle-view-btn');
    const toggleIcon = document.getElementById('toggle-icon');
    const simpleView = document.getElementById('simple-view');
    const advancedView = document.getElementById('advanced-view');
    const wifiBadge = document.getElementById('wifi-badge');

    let isAdvanced = false;

    if(toggleViewBtn) {
        toggleViewBtn.addEventListener('click', () => {
            isAdvanced = !isAdvanced;
            
            if(isAdvanced) {
                simpleView.classList.add('hidden');
                advancedView.classList.remove('hidden');
                wifiBadge.classList.remove('hidden');
                toggleIcon.classList.remove('fa-arrow-right');
                toggleIcon.classList.add('fa-arrow-left');
                toggleViewBtn.style.backgroundColor = 'var(--color-cyan)';
                toggleIcon.style.color = '#000';
            } else {
                advancedView.classList.add('hidden');
                simpleView.classList.remove('hidden');
                wifiBadge.classList.add('hidden');
                toggleIcon.classList.remove('fa-arrow-left');
                toggleIcon.classList.add('fa-arrow-right');
                toggleViewBtn.style.backgroundColor = '#176b78';
                toggleIcon.style.color = 'var(--color-cyan)';
            }
        });
    }
});