async function postRoutes(fastify, options) {
    const db = fastify.db;
    
fastify.post('/api/students/bulk-update-class', async (request, reply) => {
    const { old_class, new_class } = request.body;
    try {
        await fastify.db.run('UPDATE students SET class = ? WHERE class = ?', [new_class, old_class]);
        return { success: true, message: `Kelas ${old_class} berhasil diubah ke ${new_class}` };
    } catch (err) {
        return reply.status(500).send({ success: false, error: err.message });
    }
});

fastify.delete('/api/logs/reset', async (request, reply) => {
    try {
        await fastify.db.run('DELETE FROM attendance_logs');
        await fastify.db.run("DELETE FROM sqlite_sequence WHERE name='attendance_logs'");

        return { 
            success: true, 
            message: "Seluruh log absensi telah berhasil dikosongkan." 
        };
    } catch (err) {
        return reply.status(500).send({ success: false, error: err.message });
    }
});

fastify.get('/students-view', async (req, reply) => {
    return reply.sendFile('students-view.html');
});

fastify.post('/api/login', async (request, reply) => {
    if (!request.body) {
        console.error("[ERROR] request.body is undefined!");
        return reply.status(400).send({ success: false, message: "Missing request body" });
    }

    const { username, password } = request.body;

    const expectedUser = (process.env.ADMIN_USERNAME || 'admin').trim();
    const expectedPass = (process.env.ADMIN_PASSWORD || 'CHANGE_ME_IN_ENV').trim();

    const inputUser = (username || '').trim();
    const inputPass = (password || '').trim();

    console.log(`[DEBUG] Attempting login with: "${inputUser}" / "${inputPass}"`);
    console.log(`[DEBUG] Comparing against: "${expectedUser}" / "${expectedPass}"`);

    if (inputUser === expectedUser && inputPass === expectedPass) {
        request.session.authenticated = true;
        if (request.session.save) {
            await request.session.save();
        }
        console.log(`[AUTH] Login SUCCESS for user: ${username}`);
        return { success: true };
    } else {
        console.log(`[AUTH] Login FAILED for user: ${username}`);
        return reply.status(401).send({ success: false, message: "Invalid credentials" });
    }
});

    fastify.post('/api/absen', async (request, reply) => {
        const { rfid_uid, device_id } = request.body;
        
        if (!rfid_uid || !device_id) {
            return reply.status(400).send({ success: false, message: "Missing Data" });
        }

        try {
            const student = await db.get('SELECT name, class FROM students WHERE rfid_uid = ?', [rfid_uid]);
            
            if (!student) {
                return reply.status(404).send({ success: false, message: "Bukan Siswa" });
            }
            const today = new Date().toISOString().split('T')[0]; // Format: YYYY-MM-DD
            const existingLog = await db.get(
                'SELECT id FROM attendance_logs WHERE rfid_uid = ? AND date = ?',
                [rfid_uid, today]
            );

            if (existingLog) {
                console.log(`[ABSENSI] ${student.name} ditolak (Sudah Absen Hari Ini)`);
                return reply.status(400).send({ 
                    success: false, 
                    message: "Sudah Absen!" 
                });
            }

            await db.run(
                'INSERT INTO attendance_logs (rfid_uid, device_id, date) VALUES (?, ?, ?)', 
                [rfid_uid, device_id, today]
            );

            console.log(`[ABSENSI] ${student.name} berhasil tap di ${device_id}`);
            return { 
                success: true, 
                name: student.name,
                message: student.name 
            };
        } catch (err) {
            console.error(err.message);
            return reply.status(500).send({ success: false, message: "Server Error" });
        }
    });

    fastify.post('/api/students', async (request, reply) => {
        const { rfid_uid, name, class: className } = request.body;
        
        if (!rfid_uid || !name || !className) {
            return reply.status(400).send({ success: false, message: "Data tidak lengkap" });
        }

        try {
            await db.run(
                'INSERT INTO students (rfid_uid, name, class) VALUES (?, ?, ?)', 
                [rfid_uid, name, className]
            );
            return { success: true, message: "Siswa berhasil ditambahkan" };
        } catch (err) {
            if (err.message.includes("UNIQUE constraint failed")) {
                return reply.status(400).send({ success: false, message: "UID sudah terdaftar!" });
            }
            return reply.status(500).send({ success: false, error: err.message });
        }
    });

    fastify.delete('/api/students/:rfid_uid', async (request, reply) => {
        const { rfid_uid } = request.params;
        try {
            const result = await db.run('DELETE FROM students WHERE rfid_uid = ?', [rfid_uid]);
            
            if (result.changes === 0) {
                return reply.status(404).send({ success: false, message: "Siswa tidak ditemukan" });
            }
            
            return { success: true, message: `Siswa dengan UID ${rfid_uid} berhasil dihapus` };
        } catch (err) {
            return reply.status(500).send({ success: false, error: err.message });
        }
    });

    fastify.post('/api/students/bulk-delete', async (request, reply) => {
        const { class_name } = request.body;
        if (!class_name) {
            return reply.status(400).send({ success: false, message: "Class name is required" });
        }
        try {
            await db.run('DELETE FROM students WHERE class = ?', [class_name]);
            return { success: true, message: `Seluruh siswa di kelas ${class_name} berhasil dihapus` };
        } catch (err) {
            return reply.status(500).send({ success: false, error: err.message });
        }
    });

    // --- DEVICE MANAGEMENT ---
    fastify.get('/api/admin/devices', async (request, reply) => {
        return await db.all("SELECT *, datetime(last_seen, 'localtime') AS last_seen FROM devices ORDER BY last_seen DESC");
    });

    fastify.post('/api/admin/devices/update', async (request, reply) => {
        const { device_id, mode, power_status, display_text } = request.body;
        if (!device_id) return reply.status(400).send({ success: false, message: "Missing device_id" });

        try {
            if (mode) {
                await db.run('UPDATE devices SET mode = ? WHERE device_id = ?', [mode, device_id]);
            }
            if (power_status !== undefined && power_status !== null) {
                await db.run('UPDATE devices SET power_status = ? WHERE device_id = ?', [power_status, device_id]);
            }
            if (display_text !== undefined && display_text !== null) {
                await db.run('UPDATE devices SET display_text = ? WHERE device_id = ?', [display_text, device_id]);
            }
            
            return { success: true, message: "Konfigurasi alat berhasil diperbarui." };
        } catch (err) {
            console.error("Device Update Error:", err);
            return reply.status(500).send({ success: false, error: err.message });
        }
    });

    fastify.post('/api/device/log', async (request, reply) => {
        const { device_id, activity } = request.body;
        if (!device_id || !activity) return reply.status(400).send({ success: false, message: "Missing data" });

        try {
            await db.run('INSERT INTO device_activities (device_id, activity) VALUES (?, ?)', [device_id, activity]);
            return { success: true };
        } catch (err) {
            return reply.status(500).send({ success: false, error: err.message });
        }
    });

    fastify.post('/api/device/report-scan', async (request, reply) => {
        const { device_id, rfid_uid } = request.body;
        if (!device_id || !rfid_uid) return reply.status(400).send({ success: false, message: "Missing data" });

        try {
            await db.run('UPDATE devices SET last_scanned_uid = ?, last_seen = CURRENT_TIMESTAMP WHERE device_id = ?', [rfid_uid, device_id]);
            const student = await db.get('SELECT name, class FROM students WHERE rfid_uid = ?', [rfid_uid]);
            return { 
                success: true, 
                is_registered: !!student,
                student: student || null,
                rfid_uid: rfid_uid
            };
        } catch (err) {
            return reply.status(500).send({ success: false, error: err.message });
        }
    });

}

module.exports = postRoutes;