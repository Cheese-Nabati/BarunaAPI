

const path = require('path');
require('dotenv').config({ path: path.join(__dirname, '.env') });
const fastify = require('fastify')({ logger: true });
const sqlite3 = require('sqlite3');
const { open } = require('sqlite');

fastify.register(require('@fastify/cookie'));
fastify.register(require('@fastify/session'), {
  secret: process.env.SESSION_SECRET || 'PLEASE_SET_A_SESSION_SECRET_IN_ENV',
  cookie: { secure: false }
});

fastify.addHook('preHandler', async (request, reply) => {
    const { url, method } = request;
    const cleanUrl = url.split('?')[0].replace(/\/$/, ""); // Normalize URL

    if (cleanUrl === '/login' || cleanUrl === '/api/login' || cleanUrl.startsWith('/properties')) return;

    // These endpoints allow EITHER a valid session OR a valid Device Token
    const hardwareEndpoints = ['/api/absen', '/api/students', '/api/device/ping', '/api/device/report-scan', '/api/device/log'];
    const isHardwareApi = hardwareEndpoints.includes(cleanUrl);
    
    if (isHardwareApi) {
        if (request.session.authenticated) return; // Allow if logged in via browser
        
        const token = request.headers['x-device-token'];
        if (token === process.env.DEVICE_API_KEY) return; // Allow if valid hardware token
        
        return reply.status(403).send({ success: false, message: "Invalid Device Token" });
    }

    const protectedRoutes = ['/', '/dashboard', '/settings', '/student-view', '/recap-view', '/students-view', '/api'];
    const isProtected = protectedRoutes.some(path => cleanUrl === path || cleanUrl.startsWith(path + '/'));

    if (isProtected && !request.session.authenticated) {
        if (cleanUrl.startsWith('/api')) {
            return reply.status(401).send({ success: false, message: "Unauthorized Session" });
        }
        return reply.redirect('/login');
    }
});

async function main() {
    const db = await open({
        filename: './Database/baruna_absensi.db',
        driver: sqlite3.Database
    });
    await db.exec(`
        CREATE TABLE IF NOT EXISTS students (rfid_uid TEXT PRIMARY KEY, name TEXT, class TEXT);
        CREATE TABLE IF NOT EXISTS attendance_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT, 
            rfid_uid TEXT, 
            device_id TEXT, 
            date TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS monthly_results (id INTEGER PRIMARY KEY AUTOINCREMENT, rfid_uid TEXT, month_year TEXT, total_attendance INTEGER, UNIQUE(rfid_uid, month_year));
    `);

    await db.exec(`
        CREATE TABLE IF NOT EXISTS devices (
            device_id TEXT PRIMARY KEY,
            display_text TEXT DEFAULT 'Selamat Datang!',
            mode TEXT DEFAULT 'READER',
            power_status INTEGER DEFAULT 1,
            last_scanned_uid TEXT,
            last_seen DATETIME
        )
    `);

    await db.exec(`
        CREATE TABLE IF NOT EXISTS device_activities (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT,
            activity TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    `);

    try {
        await db.exec(`ALTER TABLE devices ADD COLUMN display_text TEXT DEFAULT 'Selamat Datang!'`);
    } catch (e) {}
    try {
        await db.exec(`ALTER TABLE devices ADD COLUMN last_scanned_uid TEXT`);
    } catch (e) {}
    try {
        await db.exec(`ALTER TABLE attendance_logs ADD COLUMN date TEXT`);
    } catch (e) {}

    fastify.decorate('db', db);

    fastify.register(require('@fastify/formbody'));
    fastify.register(require('@fastify/static'), {
        root: path.join(__dirname, 'public'),
        index: false // Prevent automatic serving of index.html at root
    });

    fastify.register(require('./src/routes/getroutes'));
    fastify.register(require('./src/routes/postroutes'));

    try {
        const port = process.env.PORT || 3000;
        await fastify.listen({ port: 3000, host: '0.0.0.0' });
        console.log(`Server Middleman Aktif di Port ${port}`);
    } catch (err) {
        console.error(err);
        process.exit(1);
    }
}

main();